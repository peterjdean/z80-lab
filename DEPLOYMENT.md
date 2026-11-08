# Z80/LAB original release-candidate staging notes (historical, pre-v1.0)

The public v1.0 procedure is in [RELEASE.md](RELEASE.md).
The original private-stage procedure below is retained for reference.

This stage is deliberately **not** a public deployment. No changes to Popper,
its existing Apache vhosts or DNS are made by the workflow. The native emulator
and browser still run client-side; there is no backend service or server-side
session storage.

## 1. Reproducible browser build on GitHub

The `.github/workflows/ci.yml` workflow now has a `web-site` job in addition
to the existing native and assembler checks. It uses Emscripten **4.0.23**,
Node **22**, `npm ci` with the committed lockfile, and checks the complete
static Emscripten site. The resulting artifact is named
`z80-lab-static-site`, containing:

- `z80-lab-site.zip`: an independently deployable site, with a
  `site-manifest.json` listing the source commit and SHA256 for each asset.
- `z80-lab-site.zip.sha256`: SHA256 of the entire site archive.

The ZIP contains an explicit allowlist of runtime files, **not** the repository,
`node_modules`, or unrelated files from the build directory. Files in the ZIP
have deterministic ordering and timestamps. The workflow uses the Git commit
SHA to associate the archive with the code that produced it. A consistent
archive layout does **not** promise byte-identical builds across different
toolchains or platforms.

**Build-time supply chain:** Emscripten and npm packages are downloaded in CI.
At runtime, Z80/LAB fetches only its same-origin static assets; no CDN,
account system or backend is required.

CI artifacts expire after 14 days; keep an approved release archive separately
when planning the public launch.

## 2. Test the GitHub artifact privately on Euler

Open the successful CI run for `feature/v1-release-readiness` in the GitHub
Actions tab and download the artifact named `z80-lab-static-site`. You may
alternatively use the GitHub CLI on Euler (replace RUN_ID with the actual run):

```sh
mkdir -p ~/z80-stage/artifact ~/z80-stage/www
gh run download RUN_ID --repo peterjdean/z80-lab \
  --name z80-lab-static-site --dir ~/z80-stage/artifact

cd ~/z80-stage/artifact
sha256sum -c z80-lab-site.zip.sha256
unzip -q z80-lab-site.zip -d ~/z80-stage/www
python3 -m json.tool ~/z80-stage/www/site-manifest.json >/dev/null
```

Start a private HTTP server from the **extracted GitHub artifact**, rather than
from Euler's local build tree:

```sh
cd ~/z80-stage/www
python3 -m http.server 8081 --bind 127.0.0.1
```

From the Mac, open an SSH tunnel in another terminal:

```sh
ssh -N -L 18081:127.0.0.1:8081 pd@euler.lan
```

Open http://127.0.0.1:18081/index.html in Safari. Hard-refresh
(Option-Command-R) if needed. Verify:

1. The application starts from the extracted package, without a public URL.
2. Assemble and run **Print a message** and **Alphabet loop**.
3. Set a source breakpoint, Step source and Step instruction; inspect memory.
4. Save a session; load different code; restore the file and resume execution.
5. Enter `FAKEOP A,B` on a known source line; confirm the red line diagnostic.
6. Inspect Safari's developer console for missing modules, MIME errors or
   failed WebAssembly loading.

A successful job proves that CI can **compile and package** the site; it does
not yet prove Safari/browser compatibility or a correct Apache deployment.

## 3. Apache requirements for a later Popper staging vhost

When a hostname, site path and access policy have been agreed, configure an
**isolated HTTPS staging location**, not the existing website document roots.
For Apache, at minimum:

- Serve `.wasm` with `Content-Type: application/wasm`.
- Serve `.mjs` with a JavaScript MIME type, e.g. `text/javascript`.
- Disable directory indexes and prevent exposure of source trees, archives,
  Git metadata, npm dependencies and secrets.
- Ensure a static HTML page never becomes stale relative to its JavaScript
  and WebAssembly assets. Until fingerprinted asset names are introduced,
  favour revalidation / conservative cache settings for HTML and modules.
- Test actual headers, HTTPS, CSP/Worker/WebAssembly compatibility and
  browser behaviour before opening access to the public.

Emscripten's own documentation specifies the `application/wasm` MIME type for
streaming compilation. No service-side execution, writable application
directories, database permissions or public SSH access are required.

**Do not deploy automatically to Popper from pull-request CI.** A later,
separately approved staging/promotion procedure should copy a verified
archive into a versioned directory and switch the serving path only after
testing and rollback preparation.
