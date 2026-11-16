# M0.3b-B1-P2.1 offline updater-bundle analysis

Status: completed offline against operator-captured artifacts. No device request,
JavaScript execution, browser, subprocess, or network operation was performed.
Raw device assets remain outside the repository.

## Artifact identity

The captured bundle was present at `/tmp/mpc-main.js`. P2.1 independently read
and hashed it:

| Artifact | Bytes | SHA-256 |
|---|---:|---|
| `mpc-main.js` | 772985 | `45219ef9fad4de46622be98ae3230e69ebbba1c86b22d50c63a2dde159cddf67` |
| captured root HTML | 587 | `19afd3e1faab83f329828b4d136fccec66fc6fd9d23a3a825a61b9a1c46f2d8e` |
| captured root headers | 168 | `08c564b9bc85af10fe259d1179c30be1fce3cdcc4c1b274c6336f0f06d581a4b` |

The bundle size and digest exactly match the operator-supplied identity. All
three files decoded as UTF-8 without replacement characters. The captured root
response identifies `AZ0X Web Server/0.5.1+git9826367`, content type
`text/html`, and content length 587. The HTML loads
`static/js/main.d096576e.js`.

Analysis command:

```sh
python3 tools/b1_p2_inspect.py \
  --headers /tmp/mpc-headers.txt \
  --html /tmp/mpc-index.html \
  --javascript /tmp/mpc-main.js \
  --observed 'GET /version.json' \
  --observed 'POST /software-update' \
  > /tmp/mpc-b1-p21-analysis.json
```

The `--observed` labels incorporate the supplied physical/runtime evidence;
the analyzer never promotes a static call to an observed endpoint by itself.

## Classification model

- **Observed active endpoint:** explicit operator-supplied runtime evidence.
- **Static application call:** a resolved `fetch()` call in the updater code;
  it does not prove that the branch ran or the server implements the target.
- **Client-library definition:** a method or URL builder on the reusable remote
  client; it does not prove construction, initialization, or server support.
- **Literal or constructed reference:** a string, template, endpoint property,
  or concatenation without a stronger call relationship.
- **Unknown:** data flow cannot be resolved conservatively.

## Updater application endpoints

The bundle contains one endpoint configuration object with these values:

| Configuration key | Target | Static use | Classification |
|---|---|---|---|
| `currentVersion` | `/version.json` | Passed through the object property to `fetch`; response JSON field `version` is read; a 4-second abort timer is installed | Observed active GET plus static application call |
| `firmwareUpload` | `/software-update` | Passed through the object property to `fetch` with a `FormData` file and a separately constructed POST option | Observed active POST plus static application call; prohibited |
| `latestVersion` | `https://inmusic.to/mpcsamplefirmwareupdate` | Passed through the object property to `fetch`; expected JSON contains `latest.version`, `latest.url`, and optional `latest.learnMore` | Static application call to an external vendor URL; runtime network success was not supplied |

The upload path accepts a selected `.img`/binary file in a `file` form field,
then transitions the UI through transfer/update states. P2.1 did not execute or
reconstruct that operation. It remains prohibited.

The current-version call and upload call are part of rendered updater behavior.
The external latest-version call is also reachable from updater code, but the
captured evidence does not establish whether the device's USB network routes it
or whether a browser successfully contacted it.

## Generic remote-object client architecture

The minified bundle defines a reusable client with configuration:

- HTTP base: `http://` plus `location.host` plus `/api/v1`;
- WebSocket host: `ws://` plus `location.host`;
- an optional idle timeout; and
- an event-emitter base.

Its methods construct these targets:

| Client operation | Construction | Intended method |
|---|---|---|
| Fetch hierarchy | `baseURL + "/subtree" + objectPath` | GET |
| Read metadata | `baseURL + "/object-meta" + objectPath` | GET |
| Read properties | `baseURL + "/object-properties" + objectPath` | GET |
| Write properties | same property target | PUT JSON |
| Invoke method | `baseURL + "/object-method" + objectPath + "/" + methodName` | POST JSON with `arguments` |
| Open notifications | `new WebSocket(wsHost)` | WebSocket upgrade |

Initialization first calls the subtree read, then opens the WebSocket and
registers error handling. A higher-level state adapter contains a reference to
that initialization method and maps the returned hierarchy into local state.

The message handler parses JSON and recognizes:

- `objectAdded`;
- `objectRemoved`;
- `objectPropertyAdded`;
- `objectPropertyRemoved`;
- `propertyValueChanged`; and
- `webAccessChanged`, which disconnects the client when disabled.

It emits local add/remove/value events and reports unknown messages. This is a
general object-tree synchronization client, not a diagnostic-specific API.

## Is the object client used by the updater?

The strongest supported conclusion is **bundled but apparently unused by the
firmware-updater UI**:

1. The bundle defines and configures one client object with the local HTTP and
   WebSocket bases.
2. The construction appears as a standalone expression; its return value is not
   assigned, exported, or passed to an updater component.
3. Construction alone calls only constructors. The base client constructor
   records configuration and message handlers; it does not call `initialize()`.
4. The bundle contains an `initialize()` invocation inside the reusable state
   adapter's own method, but static inspection found no updater call to that
   adapter method or retained client instance.
5. The rendered updater instead uses its separate endpoint object and direct
   `fetch()` calls.
6. The physical `/api/v1/subtree` request returned 404 `{}`.

Static minification prevents a formal proof of dead code. Tree-shaken bundles
can retain module side effects, aliases can defeat regex-based data flow, and a
constructor could change in another firmware. Therefore “unused” is an
evidence-based assessment, not a server-absence claim.

## WebSocket findings

Exactly one `new WebSocket(...)` constructor site is present in the generic
client and uses its configured `wsHost`. The constructor is inside
`openWebSocket()`, which is reached by the client's `initialize()` method.
No WebSocket constructor exists in the updater's version/upload workflow, and
no retained updater reference to the generic client was found.

This proves client capability only. It does not prove that a WebSocket upgrade
was attempted, that the stock server implements one, or that notifications are
available on the physical device. No WebSocket request is justified.

## Inspector improvements and limits

The P2.1 inspector now recognizes:

- literal local API paths and fragments;
- HTTP/WS base declarations;
- `.concat(...)` URL construction;
- template literals;
- static endpoint-object properties;
- direct and endpoint-property `fetch()` calls;
- nearby explicit HTTP methods;
- prototype client methods;
- WebSocket constructor sites;
- initialization definitions and invocation references; and
- explicitly supplied observed endpoints.

It masks ordinary comments, redacts query/fragment data from URLs and HTML
assets, omits source filenames, allowlists HTTP headers, bounds every input to
32 MiB, and reports malformed UTF-8 replacement counts. It deliberately does
not parse an AST, evaluate expressions, execute JavaScript, resolve general
aliases, or claim reachability from syntax alone. Minified control flow and
methods that construct request options in earlier variables can leave a static
method as `GET_OR_UNRESOLVED`; observed runtime evidence remains separate.

## Investigation decision

Further probing of the generic stock HTTP API is **not justified**. The active
updater endpoints are already understood, the object client has no evidenced
updater consumer, and the root object route returned 404. Guessing object paths,
methods, WebSocket behavior, or alternate routes would add risk without a
source-backed diagnostic hypothesis.

The saved bundle is useful evidence that the HTTP service is an updater and
that reusable framework code was included. It does not supply a mechanism to
invoke B0.2, read `/run`, validate `/proc`, or return the bounded counter report.
