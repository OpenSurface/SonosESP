# web-installer/

**Not a web page.** This directory holds the two ESP Web Tools manifests, and it
is the staging area the release workflow builds firmware into.

| File | Purpose |
|---|---|
| `manifest-4inch.json` | ESP Web Tools manifest for the 4″ panel |
| `manifest-7inch.json` | ESP Web Tools manifest for the 7″ panel |

## How it fits together

`.github/workflows/deploy-pages.yml` compiles both panel variants and copies the
artefacts here (`bootloader.bin`, `partitions.bin`, `boot_app0.bin`,
`firmware-4inch.bin`, `firmware-7inch.bin`), verifies that every part each
manifest names actually exists at the offset it claims, then stages the binaries
**and these manifests** into `docs/public/`. VitePress copies `docs/public/`
verbatim to the site root, so the manifests keep the exact `/manifest-4inch.json`
URLs that anything already linking to them expects.

The `.bin` files are build output and are gitignored — they only exist here
during a release run.

## Where the installer UI lives

`docs/guide/install.md`, which renders `<InstallPanel />`
(`docs/.vitepress/theme/InstallPanel.vue`). That component fetches these
manifests to read the version and to hand them to `<esp-web-install-button>`;
`HomeHero.vue` fetches `manifest-4inch.json` for the version badge.

There used to be a standalone `index.html` here. It was never published — the
workflow only ever uploads `docs/.vitepress/dist`, and copying it in would have
collided with the site's own home page — so it sat unreachable while drifting out
of step with the real install page. Deleted once that drift was noticed.
