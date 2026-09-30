# Steam Workshop upload requirements for a Slay the Spire (app 646570) mod

Researched 2026-09-30. Sources: the game's own `mod-uploader.jar` README (read locally), installed Workshop mods
(`ModTheSpire.json` of BaseMod and Colored Map), Steam Subscriber Agreement, Steamworks Workshop docs.
Steam pages for the Workshop legal agreement and the community upload guide (id 1767940979) could not be read
(empty page / HTTP 429), so anything about Valve's content rules below is marked as unconfirmed.

## Tool and account
- Upload with `mod-uploader.jar`, shipped in the game install:
  `.../steamapps/common/SlayTheSpire/SlayTheSpire.app/Contents/Resources/mod-uploader.jar` (macOS path).
- It uses Steamworks (steamworks4j) so the Steam client must be running and logged in to an account that owns the game.
- Java: on macOS the game bundles a JRE at `SlayTheSpire.app/Contents/Resources/jre/bin/java` (the system has no Java).
- Commands: `java -jar mod-uploader.jar new -w <workspace>` creates a workspace;
  `java -jar mod-uploader.jar upload -w <workspace>` uploads it. (Command names from search results; I could not run the
  uploader here, so option details are unverified.)
- First upload: Steam requires accepting the Steam Workshop legal agreement on the account; until then items stay hidden.

## Workspace layout (from the uploader README)
```
<workspace>/
  config.json   title, description, visibility, changeNote, tags
  content/      the files to publish: end products only (the jar), no source code
  image.jpg     preview image; replace the default
  README.md
```
`config.json` fields: `title`, `description`, `visibility` (`private` | `friends` | `friendsonly` | `public`),
`changeNote`, `tags` (the tag `tool` is reserved for tool-type mods).
The uploader writes back the published item id after the first upload, so later uploads update the same item.
Start with `"visibility": "private"` and check the item page before switching to `public`.

## Mod jar requirements (ModTheSpire)
- The mod is one jar with `ModTheSpire.json` at its root. Real examples:
  - BaseMod: `modid`, `name`, `author_list`, `description`, `credits`, `version`, `sts_version`, `mts_version`, `update_json`.
  - Colored Map: `modid`, `name`, `dependencies: ["basemod"]`, `author_list`, `description`, `version`, `ModTheSpire`, `update_json`.
- Ours needs: `modid`, `name`, `author_list`, `description`, `version`, `dependencies: ["basemod"]`,
  and `ModTheSpire` (minimum loader version). `update_json` is optional (GitHub releases URL).
- Players must also subscribe to ModTheSpire (1605060445) and BaseMod (1605833019); state this in the description.
  A Workshop item can list them as required items on its page.
- Java 8 target; the mod is loaded from the Workshop folder by ModTheSpire.

## Points that matter for a search mod
1. **Native code.** The search engine is C++. Workshop mods are normally plain jars. Shipping a per-platform executable or
   dylib/dll inside a Workshop item is technically possible (the uploader itself bundles native Steam libraries), but I found no
   Valve rule that explicitly allows or forbids it, and the Steam Subscriber Agreement excerpt does not list executables.
   Valve removes items that install third-party programs or contain malicious code (from forum reports, unconfirmed).
   Options, safest first: (a) pure Java port of the search (no native code); (b) JNI library packaged inside the jar and
   extracted to a temp dir; (c) separate executable downloaded by the player from GitHub, mod only talks to it.
   Recommendation: prototype with (c) or (b) for development, plan (a) or a clearly documented (b) for the Workshop,
   and describe exactly what runs in the item description.
2. **Intellectual property.** You must own or have rights to everything in the item. Our engine derives from sts_lightspeed (MIT):
   keep its licence text in the jar and on the page. Do not include any game jar, art, or decompiled game source.
   Use your own preview image (no game screenshots taken from promotional material without care).
3. **Licence grant.** Uploading grants Valve and its affiliates a worldwide, non-exclusive licence to use, modify and distribute
   the content (Steam Subscriber Agreement). Upload nothing you would not want redistributed.
4. **Auto-play.** Automated play inside a single-player game is allowed for mods in general, but say plainly in the
   description that it is an assistant that can play for you, and keep the default at "suggest only".
5. **Accuracy.** Our simulator is not fully consistent with the original rules (parity status INCOMPLETE). The description
   must say suggestions can be wrong.
6. Age: a Steam account holder must be 13 or older.

## Checklist before publishing
- [ ] Private upload first; install from the Workshop on a clean profile; check it loads with only ModTheSpire + BaseMod.
- [ ] `ModTheSpire.json` validated; version bumped for each upload; `changeNote` written.
- [ ] Licence texts (MIT for sts_lightspeed, ours) included.
- [ ] Description states: what it does, native components, auto-play default off, known inaccuracies, requirements.
- [ ] Preview image is our own artwork.
- [ ] Decide native-code strategy (see point 1) and confirm against Valve's current rules before going public.
