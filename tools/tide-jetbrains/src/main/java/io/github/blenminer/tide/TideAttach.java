package io.github.blenminer.tide;

import com.intellij.execution.ExecutionException;
import com.intellij.execution.ProgramRunnerUtil;
import com.intellij.execution.RunManager;
import com.intellij.execution.RunnerAndConfigurationSettings;
import com.intellij.execution.executors.DefaultRunExecutor;
import com.intellij.execution.process.ProcessInfo;
import com.intellij.notification.NotificationGroupManager;
import com.intellij.notification.NotificationType;
import com.intellij.openapi.actionSystem.ActionUpdateThread;
import com.intellij.openapi.actionSystem.AnAction;
import com.intellij.openapi.actionSystem.AnActionEvent;
import com.intellij.openapi.actionSystem.CommonDataKeys;
import com.intellij.openapi.application.ApplicationManager;
import com.intellij.openapi.progress.ProgressIndicator;
import com.intellij.openapi.progress.Task;
import com.intellij.openapi.project.Project;
import com.intellij.openapi.ui.popup.JBPopupFactory;
import com.intellij.openapi.util.UserDataHolderBase;
import com.intellij.openapi.vfs.VirtualFile;
import com.intellij.xdebugger.attach.LocalAttachHost;
import com.intellij.xdebugger.attach.XAttachDebugger;
import com.intellij.xdebugger.attach.XAttachDebuggerProvider;
import org.jetbrains.annotations.NotNull;
import org.jetbrains.annotations.Nullable;

import java.nio.file.InvalidPathException;
import java.nio.file.Path;
import java.util.ArrayList;
import java.util.List;
import java.util.Locale;
import java.util.regex.Pattern;

// Debugging a Tide game with the IDE's native debugger: CLion's, which reads a
// game's .pdb (and the natvis in it) on Windows, or LLDB elsewhere. A game's C
// maps each line back to its .tide line, so breakpoints in .tide files stop
// (TideBreakpoints lets CLion set them), and the stack is Tide's functions.
//
// - Attach to Unity (Tide): the Unity editor running this project, whose game
//   is Library/Tide's game-<n> library.
// - Debug Tide Game: the game `tide run` plays, from the Tide run
//   configuration; it starts one for the game at hand when none is running.
//   Saving a file reloads the game as it runs, still under the debugger.
//
// Both attach through the platform's attach API (Run | Attach to Process),
// preferring a native debugger over others that may claim a process.
public final class TideAttach {
    // A process, as candidates are told apart: its ID, executable and command line.
    record Running(int pid, @NotNull String executable, @NotNull String commandLine) {}

    private static final Pattern NATIVE = Pattern.compile("(?i)cidr|clion|native|cpp|lldb|gdb");

    private TideAttach() {}

    private static String folded(@NotNull String path) {
        String p = path.replace('\\', '/');
        while (p.endsWith("/")) p = p.substring(0, p.length() - 1);
        return System.getProperty("os.name", "").toLowerCase(Locale.ROOT).startsWith("windows") ? p.toLowerCase(Locale.ROOT) : p;
    }

    private static String name(@NotNull String executable) {
        final String p = executable.replace('\\', '/');
        return p.substring(p.lastIndexOf('/') + 1);
    }

    // Unity editors, the project's first: the one whose command line opens `project` (-projectPath).
    static @NotNull List<Running> unityEditors(@NotNull List<Running> all, @Nullable String project) {
        final List<Running> mine = new ArrayList<>(), others = new ArrayList<>();
        for (final Running r : all) {
            final String n = name(r.executable());
            if (!n.equalsIgnoreCase("Unity.exe") && !n.equals("Unity")) continue;
            if (project != null && folded(r.commandLine()).contains(folded(project))) mine.add(r);
            else others.add(r);
        }
        return mine.isEmpty() ? others : mine;
    }

    // The hosts `tide run` started for the game in `folder`: programs in its .tide/<configuration>/run/<n>/.
    static @NotNull List<Running> games(@NotNull List<Running> all, @NotNull String folder) {
        final String prefix = folded(folder) + "/.tide/";
        final List<Running> found = new ArrayList<>();
        for (final Running r : all) {
            final String exe = folded(r.executable());
            if (exe.startsWith(prefix) && exe.substring(prefix.length()).contains("/run/")) found.add(r);
        }
        return found;
    }

    private static @NotNull List<Running> running(@NotNull List<ProcessInfo> processes) {
        final List<Running> all = new ArrayList<>();
        for (final ProcessInfo p : processes) {
            all.add(new Running(p.getPid(), p.getExecutableCannonicalPath().orElse(p.getExecutableName()), p.getCommandLine()));
        }
        return all;
    }

    private static @Nullable ProcessInfo info(@NotNull List<ProcessInfo> processes, int pid) {
        for (final ProcessInfo p : processes) if (p.getPid() == pid) return p;
        return null;
    }

    private static void say(@NotNull Project project, @NotNull String text, @NotNull NotificationType type) {
        NotificationGroupManager.getInstance().getNotificationGroup("Tide").createNotification(text, type).notify(project);
    }

    // Attaches the native debugger to `process`, or says why none can.
    private static void attach(@NotNull Project project, @NotNull ProcessInfo process, @NotNull String what) {
        final LocalAttachHost host = LocalAttachHost.INSTANCE;
        final UserDataHolderBase context = new UserDataHolderBase();
        XAttachDebugger chosen = null;
        for (final XAttachDebuggerProvider provider : XAttachDebuggerProvider.EP.getExtensionList()) {
            if (!provider.isAttachHostApplicable(host)) continue;
            final List<? extends XAttachDebugger> debuggers = provider.getAvailableDebuggers(project, host, process, context);
            if (debuggers.isEmpty()) continue;
            if (chosen == null || NATIVE.matcher(provider.getClass().getName()).find()) chosen = debuggers.get(0);
            if (NATIVE.matcher(provider.getClass().getName()).find()) break;
        }
        if (chosen == null) {
            say(project, "No native debugger here can attach to " + what + ": debugging Tide code needs CLion's, or another "
                         + "IDE with C/C++ debugging.", NotificationType.ERROR);
            return;
        }
        final XAttachDebugger debugger = chosen;
        ApplicationManager.getApplication().invokeLater(() -> {
            try {
                debugger.attachDebugSession(project, host, process);
            } catch (ExecutionException e) {
                say(project, "Couldn't attach to " + what + ": " + e.getMessage(), NotificationType.ERROR);
            }
        });
    }

    private static @Nullable Path folderOf(@NotNull AnActionEvent event) {
        final Project project = event.getProject();
        final VirtualFile file = event.getData(CommonDataKeys.VIRTUAL_FILE);
        final String base = project != null ? project.getBasePath() : null;
        try {
            if (file != null && file.isInLocalFileSystem() && ("tide".equals(file.getExtension()) || "tprefab".equals(file.getExtension()))) {
                return TideGames.of(file.toNioPath(), base != null ? Path.of(base) : null).folder();
            }
            return base != null ? Path.of(base) : null;
        } catch (InvalidPathException | UnsupportedOperationException e) {
            return null;
        }
    }

    public static final class Unity extends AnAction {
        @Override public @NotNull ActionUpdateThread getActionUpdateThread() { return ActionUpdateThread.BGT; }

        @Override public void update(@NotNull AnActionEvent event) {
            event.getPresentation().setEnabledAndVisible(event.getProject() != null);
        }

        @Override public void actionPerformed(@NotNull AnActionEvent event) {
            final Project project = event.getProject();
            if (project == null) return;
            final String base = project.getBasePath();
            new Task.Backgroundable(project, "Finding the Unity editor") {
                @Override public void run(@NotNull ProgressIndicator indicator) {
                    final List<ProcessInfo> processes = LocalAttachHost.INSTANCE.getProcessList();
                    final List<Running> editors = unityEditors(running(processes), base);
                    if (editors.isEmpty()) {
                        say(project, "No Unity editor is running. Open the project in Unity, then attach.", NotificationType.WARNING);
                        return;
                    }
                    if (editors.size() > 1) {
                        final List<String> shown = editors.stream().map(e -> e.pid() + ": " + e.commandLine()).toList();
                        ApplicationManager.getApplication().invokeLater(() -> JBPopupFactory.getInstance()
                            .createPopupChooserBuilder(shown)
                            .setTitle("Which Unity Editor Runs the Game?")
                            .setItemChosenCallback(picked -> {
                                final ProcessInfo chosen = info(processes, editors.get(shown.indexOf(picked)).pid());
                                if (chosen != null) {
                                    ApplicationManager.getApplication().executeOnPooledThread(() -> attach(project, chosen, "the Unity editor"));
                                }
                            })
                            .createPopup()
                            .showCenteredInCurrentWindow(project));
                        return;
                    }
                    final ProcessInfo chosen = info(processes, editors.get(0).pid());
                    if (chosen != null) attach(project, chosen, "the Unity editor");
                }
            }.queue();
        }
    }

    public static final class Game extends AnAction {
        // How long a first build may take before the game's host is there to attach to
        private static final long BUILD_MS = 180_000;

        @Override public @NotNull ActionUpdateThread getActionUpdateThread() { return ActionUpdateThread.BGT; }

        @Override public void update(@NotNull AnActionEvent event) {
            event.getPresentation().setEnabledAndVisible(event.getProject() != null);
        }

        @Override public void actionPerformed(@NotNull AnActionEvent event) {
            final Project project = event.getProject();
            final Path folder = folderOf(event);
            if (project == null) return;
            if (folder == null) {
                say(project, "Open a .tide file of the game to debug.", NotificationType.WARNING);
                return;
            }
            final List<Running> already = games(running(LocalAttachHost.INSTANCE.getProcessList()), folder.toString());
            if (already.isEmpty()) {
                // Started as the Tide run configuration does: a window, and a reload whenever a file is saved
                final RunnerAndConfigurationSettings settings = RunManager.getInstance(project)
                    .createConfiguration(folder.getFileName() + " (debug)", TideRunConfigurationType.factory());
                final TideRunConfiguration configuration = (TideRunConfiguration) settings.getConfiguration();
                configuration.getOptions().setFolder(folder.toString());
                settings.setTemporary(true);
                ProgramRunnerUtil.executeConfiguration(settings, DefaultRunExecutor.getRunExecutorInstance());
            }
            new Task.Backgroundable(project, "Waiting for the game to start", true) {
                @Override public void run(@NotNull ProgressIndicator indicator) {
                    final long until = System.currentTimeMillis() + BUILD_MS;
                    while (!indicator.isCanceled() && System.currentTimeMillis() < until) {
                        final List<ProcessInfo> processes = LocalAttachHost.INSTANCE.getProcessList();
                        final List<Running> found = games(running(processes), folder.toString());
                        if (!found.isEmpty()) {
                            final ProcessInfo chosen = info(processes, found.get(found.size() - 1).pid());
                            if (chosen != null) attach(project, chosen, "the game");
                            return;
                        }
                        try {
                            Thread.sleep(250);
                        } catch (InterruptedException e) {
                            return;
                        }
                    }
                    if (!indicator.isCanceled()) say(project, "The game didn't start: tide's output in the Run tool window says why.",
                                                     NotificationType.WARNING);
                }
            }.queue();
        }
    }
}
