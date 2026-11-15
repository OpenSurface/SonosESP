# Maquettes de l'interface enfant (UI_KIDS)

Sources des maquettes de l'interface `src/square/` : carré 480×480 (ESP32-S3,
GUITION 4848S040) et portrait 480×800 (ESP32-P4).

L'original vit dans le canevas de design Claude :
https://claude.ai/artifact/F2LNLDT1zRGN18AYcT5Fe3 (pages « Carré 4″ (S3) » et
« Portrait 4″ (P4) »). Cette copie a été exportée le 2026-11-15 ; si le canevas
change, ce dossier ne suit pas tout seul.

Les fichiers `.dc.html` ont besoin du runtime du canevas (`support.js`) : ouverts
seuls dans un navigateur, ils ne s'affichent pas. Ils servent de référence
(mise en page, tailles, couleurs, textes) et peuvent être republiés dans un
canevas.

| Écran | Carré (S3) | Portrait (P4) | Code |
|---|---|---|---|
| Lecture | `Main.dc.html` | `P_Main.dc.html` | `sq_player.cpp` |
| Chercher (clavier) | `Recherche.dc.html` | `P_Recherche.dc.html` | `sq_search.cpp`, `sq_keyboard.cpp` |
| Résultats | `Resultats.dc.html` | `P_Resultats.dc.html` | `sq_search.cpp`, `sq_items.cpp` |
| Favoris | `Favoris.dc.html` | `P_Favoris.dc.html` | `sq_favs.cpp` |
| Spotify | `Spotify.dc.html` | `P_Spotify.dc.html` | `sq_browse.cpp` |
| Playlist / album | `Playlist.dc.html` | `P_Playlist.dc.html` | `sq_browse.cpp` |
| Enceintes | `Enceintes.dc.html` | `P_Enceintes.dc.html` | `sq_rooms.cpp` |
| Réglages | `Reglages.dc.html` | `P_Reglages.dc.html` | `sq_settings.cpp` |
| Premier démarrage | `Demarrage.dc.html` | `P_Demarrage.dc.html` | `main_square.cpp` |
| Paroles | `Paroles.dc.html` | `P_Paroles.dc.html` | `sq_lyrics_ui.cpp` |
| Barre du bas | `Dock.dc.html` (prop `prefix` : `""` ou `"P_"`) | | `sq_nav.cpp` |

`canvas.json` garde la disposition du canevas et les notes de conception
(une couleur par destination, favoris Sonos, décodage JPEG logiciel, paroles).
