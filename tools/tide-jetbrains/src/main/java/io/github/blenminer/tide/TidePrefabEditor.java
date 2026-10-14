package io.github.blenminer.tide;

import com.intellij.openapi.Disposable;
import com.intellij.openapi.actionSystem.AnAction;
import com.intellij.openapi.actionSystem.AnActionEvent;
import com.intellij.openapi.actionSystem.ActionUpdateThread;
import com.intellij.openapi.actionSystem.CommonDataKeys;
import com.intellij.openapi.actionSystem.CustomShortcutSet;
import com.intellij.openapi.application.ApplicationManager;
import com.intellij.openapi.fileEditor.FileDocumentManager;
import com.intellij.openapi.project.Project;
import com.intellij.openapi.ui.Messages;
import com.intellij.openapi.util.Disposer;
import com.intellij.openapi.wm.ToolWindowAnchor;
import com.intellij.openapi.wm.ToolWindowManager;
import com.intellij.ui.content.ContentFactory;
import org.jetbrains.annotations.NotNull;

import javax.imageio.ImageIO;
import javax.swing.JPanel;
import javax.swing.Timer;
import java.awt.Graphics;
import java.awt.Color;
import java.awt.event.*;
import java.awt.image.BufferedImage;
import java.io.*;
import java.nio.charset.StandardCharsets;
import java.nio.file.Path;
import java.net.ServerSocket;
import java.net.Socket;
import java.net.InetAddress;
import java.util.ArrayList;
import java.util.List;
import java.util.concurrent.TimeUnit;

// The IDE owns a hidden native editor. This panel only forwards devices and
// displays frames: documents, shaders, selection and undo stay in the C app.
public final class TidePrefabEditor extends JPanel implements Disposable {
    final Process process;
    private final ServerSocket server;
    private volatile Socket frames;
    private final String token;
    private final Timer timer;
    private final List<String> events = new ArrayList<>();
    private final java.util.Set<String> held = new java.util.HashSet<>();
    private final StringBuilder errors = new StringBuilder();
    private BufferedImage image;
    private String message = "Starting native prefab editor…";
    private volatile boolean disposed;
    private boolean waiting;

    TidePrefabEditor(Path executable, Path file, Path folder) throws IOException {
        final byte[] secret=new byte[16];new java.security.SecureRandom().nextBytes(secret);
        token=java.util.HexFormat.of().formatHex(secret);
        server=new ServerSocket(0,4,InetAddress.getByName("127.0.0.1"));server.setSoTimeout(5000);
        try {
            process = new ProcessBuilder(executable.toString(),file.toString(),"--project",folder.toString(),"--embed",String.valueOf(server.getLocalPort()),token)
                .directory(folder.toFile()).start();
        } catch (IOException e) { server.close(); throw e; }
        setFocusable(true); setBackground(new Color(19,26,32));
        setFocusTraversalKeysEnabled(false);
        final int modifier=com.intellij.openapi.util.SystemInfo.isMac ? InputEvent.META_DOWN_MASK : InputEvent.CTRL_DOWN_MASK;
        shortcut(KeyEvent.VK_S,modifier,"s"); shortcut(KeyEvent.VK_Z,modifier,"z");
        shortcut(KeyEvent.VK_Y,modifier,"y"); shortcut(KeyEvent.VK_Z,modifier|InputEvent.SHIFT_DOWN_MASK,"y");
        shortcut(KeyEvent.VK_A,modifier,"a");
        for (final var log:List.of(process.getErrorStream(),process.getInputStream())) ApplicationManager.getApplication().executeOnPooledThread(() -> {
            try (var reader = new InputStreamReader(log,StandardCharsets.UTF_8)) {
                final char[] bytes = new char[1024]; int count;
                while ((count=reader.read(bytes)) >= 0) synchronized (errors) {
                    errors.append(bytes,0,count);
                    if (errors.length()>8192) errors.delete(0,errors.length()-8192);
                }
            } catch (IOException ignored) {}
        });
        addMouseListener(new MouseAdapter() {
            @Override public void mousePressed(MouseEvent e) { requestFocusInWindow(); mouse(e); button(e,true); }
            @Override public void mouseReleased(MouseEvent e) { mouse(e); button(e,false); }
        });
        addMouseMotionListener(new MouseMotionAdapter() {
            @Override public void mouseMoved(MouseEvent e) { mouse(e); }
            @Override public void mouseDragged(MouseEvent e) { mouse(e); }
        });
        addMouseWheelListener(e -> { mouse(e); event("scroll 0 "+-e.getPreciseWheelRotation()); });
        addKeyListener(new KeyAdapter() {
            @Override public void keyPressed(KeyEvent e) { final String key=key(e); if (key!=null && held.add("key "+key)) event("key "+key+" down"); e.consume(); }
            @Override public void keyReleased(KeyEvent e) { final String key=key(e); if (key!=null) { held.remove("key "+key); event("key "+key+" up"); } e.consume(); }
            @Override public void keyTyped(KeyEvent e) { if (!e.isControlDown() && !e.isMetaDown() && !Character.isISOControl(e.getKeyChar())) event("text "+e.getKeyChar()); e.consume(); }
        });
        addFocusListener(new FocusAdapter() { @Override public void focusLost(FocusEvent e) { release(); } });
        timer = new Timer(50,e -> { if (isShowing()) request(); else release(); }); timer.start();
    }

    private void event(String text) {
        if (disposed) return;
        if (text.startsWith("mouse ") && !events.isEmpty() && events.getLast().startsWith("mouse ")) events.set(events.size()-1,text);
        else if (events.size()<200) events.add(text);
    }
    private void mouse(MouseEvent e) {
        final double scale=Math.min(getWidth()/1440.0,getHeight()/900.0);
        if (scale>0) event("mouse "+e.getX()/scale+" "+e.getY()/scale);
    }
    private void button(MouseEvent e, boolean down) {
        final String button=switch(e.getButton()) { case MouseEvent.BUTTON1 -> "left"; case MouseEvent.BUTTON2 -> "middle"; case MouseEvent.BUTTON3 -> "right"; default -> null; };
        if (button==null) return;
        final String name="button "+button;
        event(name+(down ? " down" : " up"));
        if (down) held.add(name); else held.remove(name);
    }
    private void release() { for (final String key:held) event(key+" up"); held.clear(); }
    private void shortcut(int key, int modifiers, String nativeKey) {
        new AnAction() {
            @Override public void actionPerformed(@NotNull AnActionEvent e) { release(); event("key leftCtrl"); event("key "+nativeKey); request(); }
        }.registerCustomShortcutSet(new CustomShortcutSet(javax.swing.KeyStroke.getKeyStroke(key,modifiers)),this);
    }
    private static String key(KeyEvent e) {
        if (e.getKeyCode()>=KeyEvent.VK_A && e.getKeyCode()<=KeyEvent.VK_Z) return String.valueOf((char)('a'+e.getKeyCode()-KeyEvent.VK_A));
        return switch(e.getKeyCode()) {
            case KeyEvent.VK_CONTROL,KeyEvent.VK_META -> "leftCtrl";
            case KeyEvent.VK_SHIFT -> "leftShift"; case KeyEvent.VK_ALT -> "leftAlt";
            case KeyEvent.VK_ENTER -> "enter"; case KeyEvent.VK_ESCAPE -> "escape";
            case KeyEvent.VK_BACK_SPACE -> "backspace"; case KeyEvent.VK_DELETE -> "delete";
            case KeyEvent.VK_TAB -> "tab"; case KeyEvent.VK_SPACE -> "space";
            case KeyEvent.VK_LEFT -> "leftArrow"; case KeyEvent.VK_RIGHT -> "rightArrow";
            case KeyEvent.VK_UP -> "upArrow"; case KeyEvent.VK_DOWN -> "downArrow";
            default -> null;
        };
    }

    void request() {
        if (disposed || waiting) return;
        waiting=true;
        final StringBuilder input=new StringBuilder(); int count=0;
        for (final String event:events) {
            input.append("INPUT ").append(event).append('\n'); count++;
            if (event.matches("(button|key) \\S+ (down|up)")) break;
        }
        events.subList(0,count).clear(); input.append("FRAME 1\n");
        ApplicationManager.getApplication().executeOnPooledThread(() -> {
            try {
                if (frames==null) {
                    for (;;) {
                        final Socket candidate=server.accept();candidate.setSoTimeout(5000);
                        try {
                            if (!header(candidate.getInputStream()).equals("TIDEHELLO 1 "+token)) { candidate.close();continue; }
                        } catch (IOException ex) { candidate.close();continue; }
                        frames=candidate;frames.setSoTimeout(30000);server.close();break;
                    }
                }
                final var output=process.getOutputStream(); output.write(input.toString().getBytes(StandardCharsets.UTF_8)); output.flush();
                final var stream=frames.getInputStream();
                final String line=header(stream);
                if (!line.matches("TIDEFRAME 1 [0-9]+ [01] [0-9]+ [0-9]+ [0-9]+")) throw new IOException("Incompatible prefab editor protocol. Upgrade Tide and its plugin together.");
                final int size=Integer.parseInt(line.split(" ")[2]);
                if (size<=0 || size>32*1024*1024) throw new IOException("Invalid prefab frame size.");
                final byte[] png=stream.readNBytes(size);
                if (png.length!=size) throw new IOException("Incomplete prefab editor frame.");
                final BufferedImage next=ImageIO.read(new ByteArrayInputStream(png));
                if (next==null || next.getWidth()!=1440 || next.getHeight()!=900) throw new IOException("Invalid prefab editor image.");
                ApplicationManager.getApplication().invokeLater(() -> { if (!disposed) { image=next; message=""; waiting=false; repaint(); } });
            } catch (IOException | IllegalArgumentException e) {
                final String detail; synchronized(errors) { detail=errors.toString(); }
                ApplicationManager.getApplication().invokeLater(() -> { if (!disposed) { message=e.getMessage()+" "+detail; repaint(); dispose(); } });
            }
        });
    }

    private static String header(InputStream stream) throws IOException {
        final var bytes=new ByteArrayOutputStream();int value;
        while ((value=stream.read())!='\n') {
            if (value<0 || bytes.size()>=256) throw new IOException("Native prefab editor stopped or sent an invalid header.");
            bytes.write(value);
        }
        return bytes.toString(StandardCharsets.US_ASCII);
    }

    @Override protected void paintComponent(Graphics graphics) {
        super.paintComponent(graphics);
        if (image!=null) {
            final double scale=Math.min(getWidth()/1440.0,getHeight()/900.0);
            graphics.drawImage(image,0,0,(int)(1440*scale),(int)(900*scale),null);
        }
        if (!message.isEmpty()) { graphics.setColor(Color.WHITE); graphics.drawString(message,12,20); }
    }
    @Override public void dispose() {
        if (disposed) return;
        disposed=true; timer.stop();
        try { server.close(); } catch (IOException ignored) {}
        try { process.getOutputStream().close(); } catch (IOException ignored) {}
        ApplicationManager.getApplication().executeOnPooledThread(() -> {
            try {
                if (process.waitFor(2,TimeUnit.SECONDS)) { if (frames!=null) frames.close(); return; }
            } catch (InterruptedException e) { Thread.currentThread().interrupt(); }
              catch (IOException ignored) {}
            process.descendants().forEach(ProcessHandle::destroyForcibly); process.destroyForcibly();
            try { if (frames!=null) frames.close(); } catch (IOException ignored) {}
        });
    }

    public static final class Open extends AnAction {
        @Override public @NotNull ActionUpdateThread getActionUpdateThread() { return ActionUpdateThread.BGT; }
        @Override public void update(@NotNull AnActionEvent e) { new TideOpenPrefab().update(e); }
        @Override public void actionPerformed(@NotNull AnActionEvent e) {
            final Project project=e.getProject(); final var file=e.getData(CommonDataKeys.VIRTUAL_FILE);
            if (project==null || file==null || !file.isInLocalFileSystem() || !"tprefab".equalsIgnoreCase(file.getExtension())) return;
            if (!TidelsFactory.isTrusted(project)) { Messages.showErrorDialog(project,"Trust this project before opening the prefab editor.","Tide"); return; }
            final var game=TideGames.of(file.toNioPath(),project.getBasePath()!=null ? Path.of(project.getBasePath()) : null);
            if (game.error()!=null) { Messages.showErrorDialog(project,game.error(),"Tide"); return; }
            final Path tide=Tide.installed("tide");
            if (tide==null) { Messages.showErrorDialog(project,"Couldn't find Tide. Install it: "+Tide.INSTALL,"Tide"); return; }
            final var doc=FileDocumentManager.getInstance().getCachedDocument(file);
            if (doc!=null) FileDocumentManager.getInstance().saveDocument(doc);
            final var manager=ToolWindowManager.getInstance(project);
            var window=manager.getToolWindow("Tide Prefab");
            if (window==null) window=manager.registerToolWindow("Tide Prefab",task -> {
                task.anchor=ToolWindowAnchor.RIGHT;
                task.canCloseContent=true;
                task.hideOnEmptyContent=false;
                return kotlin.Unit.INSTANCE;
            });
            for (final var content:window.getContentManager().getContents()) if (file.getPath().equals(content.getDescription())) {
                window.getContentManager().setSelectedContent(content); window.activate(null); return;
            }
            try {
                final var panel=new TidePrefabEditor(tide.resolveSibling(tide.getFileName().toString().endsWith(".exe") ? "tide-editor.exe" : "tide-editor"),file.toNioPath(),game.folder());
                final var content=ContentFactory.getInstance().createContent(panel,file.getName(),false);
                content.setDescription(file.getPath()); content.setDisposer(panel);
                Disposer.register(project,panel);
                window.getContentManager().addContent(content); window.getContentManager().setSelectedContent(content); window.activate(panel::requestFocusInWindow);
            } catch (IOException ex) { Messages.showErrorDialog(project,ex.getMessage(),"Tide"); }
        }
    }
}
