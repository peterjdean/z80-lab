# Z80/LAB public-deployment preparation

**Private staging only.** This work adds verified, versioned deployment and
hardened headers. It does not modify Popper, publish DNS, acquire certificates,
open a new public listener, or affect the existing Peter J Dean/Rowanbank sites.

## Build provenance

A successful GitHub Actions job produces the 'z80-lab-static-site' artifact
with z80-lab-site.zip and z80-lab-site.zip.sha256. Each ZIP contains
site-manifest.json with the full Git SHA and individual file hashes. Never
pair a ZIP with the checksum from another run. Retain approved releases;
GitHub Actions artifacts expire.

The installer is scripts/stage_release.py. It must be copied to Popper with
scripts/package_site.py in the SAME local directory. Only the approved static
files are extracted; no archive or deployment scripts go into DocumentRoot.

## Stage 1 — install a release without changing Apache

After CI succeeds, download the latest site artifact to Euler:

    mkdir -p ~/z80-release-hardening
    gh run download RUN_ID --repo peterjdean/z80-lab \
      --name z80-lab-static-site --dir ~/z80-release-hardening

Use scp from the Mac to transfer BOTH ZIP and checksum to Popper, along with
stage_release.py, package_site.py and test_staging_http.py from the feature
branch. Put all five files in /home/ubuntu/z80-deploy/.

On Popper:

    sudo python3 ~/z80-deploy/stage_release.py \
      --root /var/www/z80-lab install \
      --archive ~/z80-deploy/z80-lab-site.zip \
      --checksum ~/z80-deploy/z80-lab-site.zip.sha256

It verifies the archive digest, file allowlist, manifest, and individual
checksums before creating a release directory. The layout is:

    /var/www/z80-lab/
      releases/rev-<12-character-SHA>/
      current  -> releases/rev-...
      previous -> releases/rev-...    (after the second promotion)

Do not overwrite release folders manually. The installer never modifies an
existing release with mismatched contents.

View current/previous releases:

    sudo python3 ~/z80-deploy/stage_release.py --root /var/www/z80-lab status

## Stage 2 — harden the EXISTING loopback-only Apache vhost

The checked-in template is apache/z80-lab-stage-hardened.conf. It uses the
existing separate 'Listen 127.0.0.1:8082' configuration, with
DocumentRoot /var/www/z80-lab/current. Its Directory rule allows Apache to
follow that controlled symlink, disallows directory indexing, and requires
local connections. It does NOT modify the 80/443 vhosts.

Before changing Apache, BACK UP its existing stage vhost and keep the older
/var/www/z80-lab-stage content intact:

    sudo cp -a /etc/apache2/sites-available/z80-lab-stage.conf \
      /etc/apache2/sites-available/z80-lab-stage.conf.pre-hardening

Enable Apache mod_headers if missing:

    sudo a2enmod headers

Review the template, then install it at
/etc/apache2/sites-available/z80-lab-stage.conf. Run:

    sudo apache2ctl -t
    sudo apache2ctl -S
    sudo systemctl reload apache2
    sudo ss -ltnp | grep ':8082'

**Reload ONLY if the syntax check says Syntax OK.** Confirm the listener
remains 127.0.0.1:8082, never 0.0.0.0:8082 or an external address.

The template disables caching for all static files, avoiding stale modules
with fixed ZIP timestamps. It also enforces CSP, nosniff, no-referrer and
anti-framing headers. No HSTS is set on plain-HTTP staging.

The CSP allows WebAssembly only through 'wasm-unsafe-eval'; this targets
Safari 16+ because Safari 15 does not support that expression. It deliberately
avoids the broader JavaScript 'unsafe-eval' escape hatch. Test real Safari
Worker, assembler and session downloads before making this public.

## Stage 3 — verify and rehearse rollback

On Popper:

    python3 ~/z80-deploy/test_staging_http.py \
      --base http://127.0.0.1:8082
    sudo tail -n 30 /var/log/apache2/z80-lab-stage-error.log

On the Mac, via the existing SSH tunnel:

    ssh -N -L 18082:127.0.0.1:8082 popper

Open http://127.0.0.1:18082/index.html in Safari and hard refresh. Check
that both examples execute, source stepping and breakpoints work, saved
sessions restore, the '-2' file remains selectable, and invalid assembler
instructions show source-line diagnostics. Inspect the Safari console for
CSP, JavaScript module, Worker, WebAssembly and loading errors.

After a SECOND versioned release is installed, rollback is atomic:

    sudo python3 ~/z80-deploy/stage_release.py --root /var/www/z80-lab rollback

A hard Safari reload should show the earlier release; no Apache reload
normally needed. For this FIRST migration, there is no previous versioned
release: restore the backed-up stage vhost, syntax-check and reload Apache
to use the untouched /var/www/z80-lab-stage directory.

## Separate public launch decision

Only after staging works, select a dedicated subdomain. The public endpoint
should use its OWN name-based HTTPS vhost on port 443 with a valid certificate,
not expose the existing local port 8082. Review:
- DNS, TLS renewal, firewall/router port rules, and IPv6.
- Safari 16+, Chromium, Firefox and ideally mobile.
- MIME types, caching, CSP, console and access/error logs.
- DocumentRoot permissions, hidden paths, backups and release rollback.
- User documentation, license notices and privacy messaging.
- HSTS for the chosen hostname only, after HTTPS is verified; avoid
  includeSubDomains/preload unless explicitly intended.
- A final v1.0.0 tag only after public acceptance tests.

GitHub CI is NOT authorised to log in to Popper or publish automatically.
