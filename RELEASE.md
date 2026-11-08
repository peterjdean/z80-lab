# Z80/LAB v1.0.0 — production release checklist

The public site is **https://z80.peterjdean.com/**, hosted by Apache on
Popper with its own Let's Encrypt TLS certificate. This checklist promotes
an approved build into production and tags the *exact deployed Git commit*.

**Independent environments:**

- Production document root: `/var/www/z80-lab-public/current`
- Private staging document root: `/var/www/z80-lab/current`
- Private stage listener: `127.0.0.1:8082`, SSH tunnel only.
- Existing release manager: `/home/ubuntu/z80-deploy/stage_release.py`.

No change to public DNS, TLS, other websites or Apache is required to
promote a new static release. GitHub Actions never connects to Popper.

## 1. Merge and obtain the approved artifact

After the release PR checks pass, Squash and merge it. Find the successful
post-merge **main** build matching the exact merge SHA and download its
`z80-lab-static-site` artifact (ZIP plus SHA256 checksum).

On Euler, replacing RUN_ID with the successful post-merge Actions run:

    cd ~/z80-web-v0.6.0
    git fetch origin --prune
    git switch main
    git pull --ff-only origin main
    git log -1 --oneline

    mkdir -p ~/z80-v1-release
    gh run download RUN_ID --repo peterjdean/z80-lab \
      --name z80-lab-static-site --dir ~/z80-v1-release
    cd ~/z80-v1-release
    sha256sum -c z80-lab-site.zip.sha256
    unzip -p z80-lab-site.zip site-manifest.json | python3 -m json.tool

Confirm `site-manifest.json` declares `version: 1.0.0` and the exact
post-merge SHA in `revision`. The ZIP/checksum must come from the same run.

## 2. Deploy FIRST to private staging

From the Mac:

    scp -3 pd@euler.lan:~/z80-v1-release/z80-lab-site.zip popper:~/z80-deploy/
    scp -3 pd@euler.lan:~/z80-v1-release/z80-lab-site.zip.sha256 popper:~/z80-deploy/

On Popper:

    cd ~/z80-deploy
    sha256sum -c z80-lab-site.zip.sha256

    sudo python3 stage_release.py --root /var/www/z80-lab install \
      --archive ~/z80-deploy/z80-lab-site.zip \
      --checksum ~/z80-deploy/z80-lab-site.zip.sha256
    sudo python3 stage_release.py --root /var/www/z80-lab status

    python3 test_staging_http.py --base http://127.0.0.1:8082

On the Mac, access the private site via:

    ssh -N -L 18082:127.0.0.1:8082 popper

Open `http://127.0.0.1:18082/index.html` in Safari. Hard refresh and check:

- Browser footer shows v1.0.0; Help & guide opens `help.html`.
- Print a message and Alphabet loop assemble, load, execute correctly.
- Breakpoints, source stepping, instruction stepping, diagnostics work.
- Save and restore session, including old Safari-suffixed filenames.
- Browser console has no new CSP, Worker or WebAssembly load failures.

If the staging update fails, revert *only staging*:

    sudo python3 stage_release.py --root /var/www/z80-lab rollback

## 3. Explicit production promotion

Only after staging acceptance, independently install the same ZIP in production:

    sudo python3 ~/z80-deploy/stage_release.py \
      --root /var/www/z80-lab-public install \
      --archive ~/z80-deploy/z80-lab-site.zip \
      --checksum ~/z80-deploy/z80-lab-site.zip.sha256

    sudo python3 ~/z80-deploy/stage_release.py \
      --root /var/www/z80-lab-public status

This changes only the production release symlink, not Apache configuration.
Verify the `Current` revision matches the ZIP manifest SHA. If a public
regression is discovered:

    sudo python3 ~/z80-deploy/stage_release.py \
      --root /var/www/z80-lab-public rollback

## 4. External acceptance and operational preflight

Test `https://z80.peterjdean.com/` in a real browser from outside the home
network, with a hard refresh. Load `help.html` and rerun the main acceptance
sequence. Verify from Popper:

    curl -fsSI https://z80.peterjdean.com/
    curl -fsSI https://z80.peterjdean.com/help.html
    curl -fsSI https://z80.peterjdean.com/z80wasm.wasm

Look for HTTP 200, `Content-Type: application/wasm` on the WASM asset,
`Cache-Control: no-store` and the approved Content Security Policy.

Check HTTP-to-HTTPS redirection, ensuring the ACME challenge path remains
available over HTTP. If not already verified:

    sudo certbot renew --cert-name z80.peterjdean.com --dry-run

Confirm `peterjdean.com` and `rowanbank.net` are unaffected, and arrange
external URL/TLS monitoring. Retain the approved ZIP/checksum independently
because GitHub Actions artifacts expire.

## 5. Tag the deployed commit LAST

Only after public tests succeed and production serves this exact v1.0.0
commit, on Euler:

    cd ~/z80-web-v0.6.0
    git fetch origin --tags
    git switch main
    git pull --ff-only origin main
    git rev-parse HEAD

Check HEAD is identical to the installed production manifest revision. If
`v1.0.0` does not already exist and the working tree is clean:

    git tag -a v1.0.0 -m "Z80/LAB v1.0.0"
    git push origin v1.0.0

Publish a GitHub Release for this tag and attach the same verified archive
and checksum. Using the GitHub CLI on Euler:

    gh release create v1.0.0 \
      ~/z80-v1-release/z80-lab-site.zip \
      ~/z80-v1-release/z80-lab-site.zip.sha256 \
      --repo peterjdean/z80-lab \
      --title "Z80/LAB v1.0.0" \
      --notes-file CHANGELOG.md

This completes the stable release without retagging or rebasing a published SHA.
