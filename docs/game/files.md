---
description: "Read and write local files in Tide, use persistent save and temporary cache paths, and fetch URL bytes from views, handlers or tasks."
---

# Files and directories

`File` and `Directory` read and change this machine's files, and `Web.Fetch` gets a URL's bytes. Use them from views, local handlers and local tasks, or functions those call. They cannot run in the match, in input sampling, on the GPU or inside a parallel loop. A service or file's contents reach the match through [input](./input.md) or a [command](./commands.md).

## Where to save

`Application.persistentDataPath` is the game's save directory. Its contents survive normal restarts and application updates. `Application.temporaryCachePath` is for disposable files; code must tolerate their disappearance. Both are read-only runtime properties.

Save locations use the game's stable `appId` setting, independently of its window title:

```csharp
settings { appId = "com.studio.game"; }

void WriteSave(string contents) fails IOError
{
    var path = Path.Combine(Application.persistentDataPath, "save.txt");
    try File.WriteAllText(path, contents);
}
```

Without an explicit `appId`, the standard host derives one from the game's folder or target name. Set one before shipping: changing it changes which save directory the game uses.

Relative paths resolve under the persistent data directory on every platform. Explicit absolute paths are usable on native platforms subject to the OS's permissions. On the web, files stay inside this game's persistent and temporary virtual roots.

## Whole-file operations

| Call | Behavior |
| --- | --- |
| `File.Exists(path)` | Whether a file exists; false if the path is inaccessible or invalid. |
| `File.Create(path)` | Create an empty file, replacing its contents if it exists. |
| `File.ReadAllText(path)` | Read UTF-8 text; invalid UTF-8 fails with `IOError.InvalidData`. |
| `File.WriteAllText(path, text)` | Create or replace a UTF-8 file. |
| `File.AppendAllText(path, text)` | Append UTF-8 text, creating the file if necessary. |
| `File.ReadAllBytes(path)` | Read the exact contents as a `List<byte>`. |
| `File.WriteAllBytes(path, bytes)` | Create or replace a file with a `List<byte>`. |
| `File.Copy(source, destination[, overwrite])` | Copy a file; overwriting is off by default. |
| `File.Move(source, destination[, overwrite])` | Move or rename a file; overwriting is off by default. |
| `File.Delete(path)` | Delete a file; an absent file succeeds. |

Writing a file does not create its parent directories. Use `Directory.CreateDirectory` first. `byte` is an unsigned integer from 0 to 255, so binary files can contain every byte, including zero, without text encoding.

```csharp
void WriteBinary() fails IOError
{
    var folder = Path.Combine(Application.persistentDataPath, "saves");
    try Directory.CreateDirectory(folder);
    try File.WriteAllBytes(Path.Combine(folder, "data.bin"), [0, 128, 255]);
}
```

## Directories and paths

| Call | Behavior |
| --- | --- |
| `Directory.Exists(path)` | Whether a directory exists. |
| `Directory.CreateDirectory(path)` | Create missing parents; an existing directory succeeds. |
| `Directory.GetFiles(path[, recursive])` | A `List<string>` of file paths. |
| `Directory.GetDirectories(path[, recursive])` | A `List<string>` of directory paths. |
| `Directory.Move(source, destination)` | Move or rename a directory; the destination must be absent. |
| `Directory.Delete(path[, recursive])` | Delete a directory. Nonempty directories require `recursive = true`. |

Enumeration returns complete paths, sorted by their UTF-8 bytes. Recursion is off by default; native recursive traversal does not follow directory links. A missing directory is an error.

`Path.Combine(a, b)` joins paths with a separator, using the second path by itself if it is absolute. `Path.GetFileName(path)`, `Path.GetDirectoryName(path)` and `Path.GetExtension(path)` work on the path's text without accessing the filesystem. Both `/` and `\` are recognized as separators. These pure operations can also run in match code.

## Errors and persistence

`File` and `Directory` operations other than `Exists` use [errors as values](../language/errors.md): `fails IOError`. A missing file gives `IOError.NotFound`; reading an empty file succeeds with empty text or an empty list. Other error members are `AlreadyExists`, `AccessDenied`, `InvalidPath`, `NotDirectory`, `DirectoryNotEmpty`, `OutOfSpace`, `StorageUnavailable`, `InvalidData` and `Failed`.

On the web, a virtual filesystem is loaded from IndexedDB before the game starts. Reads and mutations then operate immediately in memory. Persistent changes are committed automatically in the background, and hot reload retains the files and pending commits. Temporary files are held only by the page.

`await Storage.Flush()` confirms that save changes made before the call have committed. It is a local asynchronous operation that can fail with `IOError`; it does not freeze the frame loop. On native platforms it flushes the affected files and directory changes through the operating system.

```csharp
local singleton SaveStatus { bool saved; string message; }

async void Save(string contents, mut SaveStatus status)
{
    status.saved = false;
    if (WriteSave(contents) is IOError error)
    {
        status.message = "Write failed: " + error;
        return;
    }
    if (await Storage.Flush() is IOError error)
    {
        status.message = "Save failed: " + error;
        return;
    }
    status.saved = true;
    status.message = "Saved";
}
```

Wait for a flush before showing that a save is finished. Do this during play: closing a browser page cannot reliably finish pending work. A failed commit retains its in-memory changes, so another flush can retry them. Browser storage belongs to the site's origin and browser profile, and may be cleared by the user or evicted by the browser. Unavailable persistent storage is reported as an error; the temporary directory remains usable.

## Fetching a URL

`await Web.Fetch(url)` gets what's at an `http://` or `https://` URL, as a `List<byte>`. It's local and asynchronous, like `Storage.Flush`: the task waits while this machine fetches, frames go on, and the bytes arrive in a frame to come. It fails with `IOError`:

| Result | When |
| --- | --- |
| `IOError.NotFound` | The server answered 404. |
| `IOError.InvalidPath` | The URL doesn't start with `http://` or `https://`. |
| `IOError.Failed` | Anything else: no connection, a certificate the system doesn't trust, another status, a body over 64 MiB, or more than 5 redirects. |

Redirects are followed. On the web it's the page's `fetch`, so the server's CORS rules apply, and a refusal is `Failed`. Natively it's an HTTP client of the engine's own over the system's TLS. Nothing times out: a server that never answers is a fetch that never ends, as a browser's is.

## Sending a request

`Web.Fetch` is a GET that wants a page. To talk to a service, build a request and send it: `Web.Request(url)` gives a `WebRequest`, each call on it gives the request with one more thing set, and `await request.Send()` sends it.

```csharp
async void Report(string url, List<byte> score, mut Board board)
{
    var answer = await Web.Request(url)
        .SetMethod("POST")
        .SetHeader("Content-Type", "application/octet-stream")
        .SetBody(score)
        .Send();
    if (answer is WebResponse response)
    {
        board.accepted = response.status == 200;
        board.reply = response.body;
    }
    else board.accepted = false; // No answer came
}
```

| Call | What it sets |
| --- | --- |
| `Web.Request(url)` | A request to that URL: a GET, with no headers and no body |
| `SetMethod(method)` | The method, like `"POST"`, `"PUT"` or `"DELETE"`, in any case |
| `SetHeader(name, value)` | One more header |
| `SetBody(bytes)` | The body, a `List<byte>` |
| `await request.Send()` | Sends it, and gives a `WebResponse`: its `status` and its `body` |

A request is a value, like any struct: a call changes nothing where it's written, it gives a new request. So a request kept in a variable is built on by assigning, `request = request.SetHeader("Accept", "text/plain");`, and its fields can be set as they are (`request.method = "PUT";`). Keep one and send it as often as you like.

**An answer is an answer, whatever its status.** A 404 or a 500 comes back as a `WebResponse` with that `status` and the body the server sent, for the game to read. `Send` fails only when no answer came:

| Result | When |
| --- | --- |
| `IOError.InvalidPath` | The URL doesn't start with `http://` or `https://`. |
| `IOError.InvalidData` | The request can't be sent as it's written: a method that isn't a word of letters, or is `CONNECT`, `TRACE` or `TRACK`; a header whose name isn't a token or whose value has more than printable ASCII; a body over 64 MiB. |
| `IOError.Failed` | Anything else: no connection, a certificate the system doesn't trust, an answer over 64 MiB, more than 5 redirects, or on the web a server whose CORS refuses the page. |

The rest is `Web.Fetch`'s: local and asynchronous, the page's `fetch` on the web and the engine's own client natively, and nothing times out. Redirects are followed as a browser follows them: a 303, and a POST's 301 or 302, become a GET of where they point, and a 307 or 308 is the same request again. `Host`, `Content-Length`, `Connection` and `Transfer-Encoding` are the sender's own to say, so setting them does nothing, and a browser keeps a few more to itself (`User-Agent` among them). A line break in a header's name or value becomes a space. On the web, a request with headers of its own or a method beyond GET and POST is one the server has to allow (CORS), as for any page.

What a machine fetches is its own, like a file it reads. A picture every player should see goes into the match through a command, and each machine decodes it there (see [Assets](./assets.md)):

```csharp
local singleton Download { bool busy; string message; }

async void Load(string url, mut Download download)
{
    download.busy = true;
    if (await Web.Fetch(url) is IOError error)
    {
        download.message = "Couldn't fetch: " + error;
    }
    else download.message = "Fetched";
    download.busy = false;
}
```
