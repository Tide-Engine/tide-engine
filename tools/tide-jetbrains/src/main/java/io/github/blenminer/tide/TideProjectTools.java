package io.github.blenminer.tide;

import com.google.gson.JsonObject;
import com.google.gson.JsonParser;
import org.jetbrains.annotations.NotNull;
import org.jetbrains.annotations.Nullable;

import java.nio.file.Files;
import java.nio.file.Path;
import java.security.MessageDigest;
import java.util.HexFormat;
import java.util.regex.Pattern;

// The same installation Unity selected, or a standalone game's tide.packages pin. Never fall back to another
// version after finding a pin. Unity owns commit resolution/downloads; IDEs read its commit-keyed local cache.
final class TideProjectTools {
    record Found(@Nullable Path file, @Nullable String error, @Nullable ProcessHandle session) {
        Found(Path file, String error) { this(file, error, null); }
    }
    private static final java.util.Set<Long> sessions = java.util.concurrent.ConcurrentHashMap.newKeySet();
    private static final Pattern VERSION = Pattern.compile("\\d+\\.\\d+\\.\\d+(?:-[0-9A-Za-z.-]+)?");
    private static final Pattern PIN = Pattern.compile("(?m)^\\s*tide\\s*=([^\\r\\n]*)");

    private TideProjectTools() {}

    static @NotNull Found find(@Nullable Path folder, @NotNull String name) {
        final String home = System.getProperty("user.home");
        final String local = System.getenv("LOCALAPPDATA");
        final Path installed = com.intellij.openapi.util.SystemInfo.isWindows
            ? (local != null ? Path.of(local, "Tide") : null) : Path.of(home, ".tide");
        final String set = System.getenv("TIDE_VERSIONS");
        final Path versions = set != null && !set.isEmpty() ? Path.of(set)
            : installed != null ? installed.resolve("versions") : null;
        final Found found = find(folder, name, installed, versions);
        final ProcessHandle session = found.session();
        if (session != null && sessions.add(session.pid())) {
            session.onExit().thenRun(() -> {
                sessions.remove(session.pid());
                final var app = com.intellij.openapi.application.ApplicationManager.getApplication();
                if (app != null && !app.isDisposed()) app.invokeLater(TidelsFactory::restartAll);
            });
        }
        return found;
    }

    // Explicit installation roots keep tests independent of this machine's Tide installation.
    static @NotNull Found find(@Nullable Path folder, @NotNull String name, @Nullable Path installed, @Nullable Path versions) {
        String wanted = null;
        boolean manifestSeen = false;
        for (Path root = folder != null ? folder.toAbsolutePath().normalize() : null; root != null; root = root.getParent()) {
            final Path lockPath = root.resolve("Packages/packages-lock.json");
            final JsonObject lock = json(lockPath);
            final JsonObject dependencies = object(lock, "dependencies");
            final JsonObject tide = object(dependencies, "com.tide.unity");
            final JsonObject selected = tide != null ? json(root.resolve("Library/Tide/toolchain.json")) : null;
            final String lockHash = string(selected, "lockHash");
            final String commit = string(tide, "hash");
            final boolean matches = number(selected, "format") == 2 && (lockHash != null ? lockHash.equals(hash(lockPath))
                : tide != null && "git".equals(string(tide, "source")) && commit != null && commit.equals(string(selected, "commit")));
            final long sessionPid = number(selected, "sessionPid");
            final ProcessHandle session = sessionPid > 0 ? activeSession(sessionPid, number(selected, "sessionStarted")) : null;
            if (matches && (sessionPid == 0 || session != null)) {
                final String command = string(selected, "tide");
                try {
                    if (command != null && Path.of(command).isAbsolute() && Path.of(command).getParent() != null) {
                        final Path file = Path.of(command).getParent().resolve(Tide.exe(name));
                        final String version = string(selected, "version");
                        final Path installation = file.getParent().getParent();
                        // The global installation may have been upgraded since Unity selected it. Resolve the pin again.
                        if (sessionPid > 0 || (installation != null && valid(version) && version.equals(trim(read(installation.resolve("VERSION")))))) {
                            return Files.isRegularFile(file) ? new Found(file, null, session)
                                : new Found(null, file + " is missing. Open Unity and rebuild with the project's Tide package.", session);
                        }
                    }
                } catch (java.nio.file.InvalidPathException e) {
                    return new Found(null, "Invalid Tide toolchain path. Open Unity and select a valid tide command.");
                }
            }
            if (tide != null && "git".equals(string(tide, "source"))) {
                final String revision = string(tide, "version");
                final String tag = revision != null && revision.contains("#") ? revision.substring(revision.lastIndexOf('#') + 1).replaceFirst("^v", "") : "";
                final JsonObject cached = json(root.resolve("Library/Tide/version.json"));
                wanted = valid(tag) ? tag : commit != null && commit.equals(string(cached, "commit")) ? string(cached, "version") : null;
                if (!valid(wanted)) return new Found(null, "Open this project in Unity to resolve and install its Tide package's version, then restart the Tide language server.");
                break;
            }
            if (Files.exists(lockPath) && lock == null) return new Found(null, "Couldn't read " + lockPath + ". Fix the package lock before starting Tide tooling.");
            final String manifest = read(root.resolve("tide.packages"));
            if (!manifestSeen && manifest != null) {
                manifestSeen = true;
                final var pins = PIN.matcher(manifest);
                if (pins.find()) {
                    wanted = pins.group(1).split("#", 2)[0].trim().replaceFirst("^v", "");
                    if (!valid(wanted) || pins.find()) return new Found(null, "Invalid Tide version pin in " + root.resolve("tide.packages") + ". Write tide = <whole version>.");
                }
            }
        }
        if (wanted == null) return new Found(Tide.installed(name), null);
        for (final Path place : new Path[]{versions != null ? versions.resolve(wanted) : null, installed}) {
            if (place == null) continue;
            final Path file = place.resolve("bin").resolve(Tide.exe(name));
            if (wanted.equals(trim(read(place.resolve("VERSION")))) && Files.isRegularFile(file)) return new Found(file, null);
        }
        return new Found(null, "Tide " + wanted + " is required by this project. Open Unity to install its package's version, or install that version of Tide; no other version will be used.");
    }

    private static boolean valid(String value) { return value != null && VERSION.matcher(value).matches(); }
    private static ProcessHandle activeSession(long pid, long started) {
        return ProcessHandle.of(pid).filter(process -> process.isAlive()
            && process.info().startInstant().map(time -> time.getEpochSecond() == started).orElse(false)).orElse(null);
    }
    private static long number(JsonObject value, String name) {
        try { return value != null && value.has(name) ? value.get(name).getAsLong() : 0; }
        catch (RuntimeException e) { return -1; }
    }
    private static String trim(String text) { return text != null ? text.trim() : null; }
    private static String hash(Path file) {
        try { return HexFormat.of().formatHex(MessageDigest.getInstance("SHA-256").digest(Files.readAllBytes(file))); }
        catch (Exception e) { return null; }
    }
    private static String read(Path file) { try { return Files.readString(file); } catch (Exception e) { return null; } }
    private static JsonObject json(Path file) {
        try { return JsonParser.parseString(Files.readString(file)).getAsJsonObject(); } catch (Exception e) { return null; }
    }
    private static JsonObject object(JsonObject value, String name) {
        return value != null && value.has(name) && value.get(name).isJsonObject() ? value.getAsJsonObject(name) : null;
    }
    private static String string(JsonObject value, String name) {
        return value != null && value.has(name) && value.get(name).isJsonPrimitive() ? value.get(name).getAsString() : null;
    }
}
