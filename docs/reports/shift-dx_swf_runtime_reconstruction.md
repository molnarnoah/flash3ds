# Shift-DX (3DS) — beágyazott SWF/Flash runtime feltérképezése

**Cél:** a beágyazott SWF-lejátszót ("gameswf" alapú) annyira megérteni és izolálni, hogy hosszú távon önálló 3DS "Flash Virtual Console" runtime építhető legyen belőle.
**Módszer:** minden állítás egy konkrét Ghidra-bizonyítékra (string-xref + dekompilátum, vagy — ahol jelölve van — kontrollfolyam-mintázat) vezethető vissza. Ahol a bizonyíték nem elég erős egy végleges névhez, azt kifejezetten jelzem, és NEM neveztem át semmit spekulatívan.
**Fontos korlát, ami sok pontot befolyásol:** a jelenlegi Ghidra-MCP eszközkészletben **nincs "nyers memória/adat kiolvasás" funkció**. Ahol a kód egy literál-pool szóból tölt be egy natív függvénymutatót (pl. `ldr r1,[DAT_xxxxxxx]`), a dekompilátum és az xref-lekérdezések is csak a *slot címét* mutatják, az ott tárolt *értéket* nem. Ez a #1 technikai akadály a natív kötések (ExternalInterface, AVM1 built-in metódusok, tag-loader tábla) pontos címeinek feloldásához — ld. a 9. pont végén a javasolt megoldást.

---

## TASK 1 — A runtime megtalálása (string-alapú bizonyítékok)

| String | Cím | Hivatkozó függvény | Dekompilálva | Kiértékelés |
|---|---|---|---|---|
| `"Playing %s, swf version %d\n"` | `0x002dc2c8` | `FUN_002dc0b4` | ✅ | Movie-létrehozás belépési pontja |
| `"error: can't create a movie from '%s'\n"` | `0x002dc2e4` | `FUN_002dc0b4` (ua.) | ✅ | ua. |
| `"error: can't create movie instance\n"` | `0x002dc310` | `FUN_002dc0b4` (ua.) | ✅ | ua. |
| `"version = %d, file_length = %d\n"` | `0x002b4888` | `FUN_002b45b8` | ✅ | SWF fejléc-parszolás |
| `";frame rate = %f, frames = %d\n"` | `0x002b48ab` | `FUN_002b45b8` (ua.) | ✅ | ua. |
| `"*** no tag loader for type %d\n"` | `0x002b4e44`, `0x002c19f0` | `gameswf_movie_def_impl_read_tags` (0x2b4b8c), `FUN_002c189c` | ✅ (előző menetben) | Tag-diszpécser fallback |
| `"-------------- actions in frame %d\n"` | `0x002bfa44` | cím `0x2bf900`-ra hivatkozik, de **nincs önálló Ghidra-függvény ezen a címen** | ⚠️ | Ld. "Nyitott kérdések" |
| `"start_sound tag: id=%d, stop = %d, loop ct = %d\n"` | `0x002c3668` | forráscím `0x2c3638`, **szintén nincs önálló függvény-határ ezen a címen** | ⚠️ | Ld. "Nyitott kérdések" |
| `"doABC tag loader, abc_name = '%s'\n"` | `0x002c0904` | `gameswf_do_abc_loader` (0x2c0828) | ✅ (előző menetben) | AS3 DoABC betöltő |
| `"ExternalInterface"` | `0x0027d78c`, `0x003cb9fc` | `as_global_externalinterface_init` (0x27d584) | ✅ | ld. TASK 5 |
| `"addCallback"` | `0x0027d7a4` | `as_global_externalinterface_init` (ua.) | ✅ | ua. |
| `"removeCallback"` | `0x0027d7b4` | `as_global_externalinterface_init` (ua.) | ✅ | ua. |
| `"Stage"` | *(ebben a menetben nem sikerült egyértelmű xreffel párosítani — a szó túl gyakori/rövid, a `list_strings filter` API a `%`-jeles és néhány rövid mintát nem szűr meg megbízhatóan)* | — | ❌ | Nyitott |
| `"RASTER_IMAGE"` | `0x003cbb88` | **nincs code-xref** (adat-only, feltehetően egy név↔enum stringtáblában) | ⚠️ | ld. TASK 6 |
| `"SPRITE_OBJECT"` | *(ebben a menetben nem található külön, csak `SPRITE_OBJECT_MOUSE_STATE_*` és `SPRITE_OBJECT` prefixű összetett kulcsok, ld. előző menet)* | — | ⚠️ | Nyitott |
| `"SPRITE_RENDERER"` | `0x003cbbc8` | **nincs code-xref** | ⚠️ | ld. TASK 6 |

**Extra, ebben a menetben talált, nem kért, de nagyon releváns stringek:**

| String | Cím | Hivatkozó függvény |
|---|---|---|
| `":\tunknown opcode 0x%02X\n"` | `0x002ba9fc` | `FUN_002ae784` |
| `"TODO opcode 0x%02X\n"` | `0x002ae570` | `FUN_002ab2d0` (ld. TASK 4 — méret-anomália) |
| `"opcode 0x%X: invalid stack error, needs %d elements but only has %d\n"` | `0x003cbd24` | nincs code-xref (dead/inline terület) |
| `"gotoAndPlay"` | `0x002d2c0c` | `avm1_builtin_prototypes_init` (0x2d228c) |

---

## TASK 2 — Függőségi gráf (eddig igazolt élek)

```
gameswf_create_movie_instance (0x2dc0b4)
 ├─ movie_definition factory (FUN_001944ec)           [SWF_Load — mozifájl-gyorsítótár/betöltés]
 ├─ vtable+0x40  → create_instance()                   [SWF_CreateMovie]
 └─ vtable+0x50  → set_root()/attach()

gameswf_movie_def_impl_read_header (0x2b45b8)          [SWF_ParseHeader]
 ├─ signature/verzió/hossz olvasás
 ├─ FUN_000985a0                                        [zlib inflate — 'CWS' tömörített SWF esetén]
 ├─ FUN_0023d9f8 + FUN_002dcc90                          [stage rect olvasás]
 ├─ FUN_00192998 ×2                                      [frame-rate / frame-count feldolgozás]
 └─ gameswf_movie_def_impl_read_tags (0x2b4b8c)          [SWF_ParseTags belépési pont]
      ├─ gameswf_stream_open_tag (0x2dcb48)              [tag fejléc olvasás]
      ├─ tag_loaders hash lookup (DAT_002b4e40)          [SWF_TagDispatcher — tartalom nem feloldva, ld. TASK 3]
      │    └─ (tag loader function pointer hívása)
      └─ gameswf_stream_close_tag (0x2dcc34)

gameswf_sprite_definition_read_tags (0x2c189c)          [beágyazott DefineSprite tag-hurok]
 └─ get_tag_loader (FUN_002b35d8) → loader hívás

gameswf_do_abc_loader (0x2c0828)                        [DoABC / AS3 betöltő]
 ├─ FUN_00222fdc                                         [abc_def objektum allokálása]
 └─ FUN_002ddffc, FUN_002dd7c8                            [ABC blokk beolvasása és regisztrálása]

as_global_externalinterface_init (0x27d584)             [ExternalInterface AS objektum]
 ├─ FUN_0022ce64 ×n                                       [AS string-konstansok létrehozása]
 ├─ FUN_0022cd3c / FUN_0022273c                            [set_member — natív metódus regisztrálása]
 └─ FUN_001c9f64 ×3                                        [natív fv.-mutató → as_value csomagolás:
                                                              addCallback / removeCallback / "call"(?)
                                                              — CÍMEK NEM FELOLDVA, ld. TASK 5]

avm1_builtin_prototypes_init (0x2d228c)                  [Object/String/MovieClip prototípusok]
 └─ FUN_001c9f64 ×~55                                      [minden AS2 built-in metódus natív kötése
                                                              — CÍMEK NEM FELOLDVA, ld. TASK 4]

gameswf_button_register_mouse_state_names (0x27eed8)     [gomb hit-test állapotgép névtáblája]
```

A feladatkiírásban kért node-nevek (`SWF_DefineShape`, `SWF_DefineBitmap`, `SWF_DefineText`, `AVM1_Stack`, `DisplayList_Add/Remove/Replace`, `RasterImage`, `ShapeRenderer`, `TextRenderer`, `StageRenderer`, `Sound_*`, `Keyboard`, `Mouse`, `Touch` stb.) **koncepcionálisan** megfeleltethetők a fentieknek, de konkrét címhez ebben a menetben csak részben tudtam kötni őket — ezt lásd soronként a TASK 3/6/7/8 táblázatokban ("konfirmált" / "gyanított, cím ismert" / "nem található").

Egy erős, új, kontrollfolyam-alapú (nem string-alapú) jelölt a shape-rendereléshez:

- **`FUN_002c37f8`** — bitfolyam-alapú rajzoló-rekord olvasó: fill/line style tömbök inicializálása RGBA alapértékkel, majd egy ciklusban a klasszikus SWF `SHAPERECORD` flag-mintázatot ellenőrzi (`new_styles`, `line_style`, `fill_style1`, `fill_style0`, `move_to` bitek egyenkénti tesztje `uVar2 << 0x1c/0x1d/0x1e` formában), és `param_3 (tag_type) == 0xb` (=`DefineText`=11) esetén más edge-olvasó ágat választ (`FUN_001a0f80` vs `FUN_001a0f38`) — ez pontosan illik a gameswf közös shape/glyph-edge parszolójára (`SWF_DefineShape` **és** `SWF_DefineText` glyph-outline is ezen megy át). **Nem neveztem át**, mert az egyetlen bizonyíték kontrollfolyam-mintázat, nincs hozzá string-horgony — de magas megbízhatóságú jelölt.

---

## TASK 3 — SWF tag-diszpécser

A diszpécser hurok (`gameswf_movie_def_impl_read_tags`, 0x2b4b8c) így működik:

1. `gameswf_stream_open_tag` kiolvassa a 16 bites tag-fejlécet → `tag_type` (10 bit), `tag_length` (6 bit, vagy kiterjesztett 32 bit, ha `0x3F`).
2. Ha `tag_type == 0` → `TAG_END`, kilépés/frame-vég kezelés.
3. Egyébként: `piVar1 = DAT_002b4e40` — **egy hash-tábla mutatója** (`hash<int, loader_function>` jellegű szerkezet), amiből a `tag_type` alapján keres egy loader-függvénymutatót, majd meghívja.
4. `gameswf_stream_close_tag` ellenőrzi, hogy a stream pozíció egyezik-e a tag várt végével.

**A hash-tábla tartalmát (a konkrét tag ID → handler cím párokat) ebben a menetben nem sikerült kiolvasni**, mert:
- a `DAT_002b4e40` globálisra csak **1 db READ xref** található (`gameswf_movie_def_impl_read_tags`-ból), **írás/inicializáló xref nincs** — vagyis a hash-tábla feltehetően egy nagyobb, még nem lokalizált "regisztráló" függvényben épül fel (a gameswf forrásában ez az `ensure_loaders_registered()` / `add_tag_loader()` minta, tipikusan 50-90 egymást követő hívással), amit ebben a menetben nem találtam meg;
- a hash bucket-tömb nyers tartalmának kiolvasásához adat-dump kellene, ami a jelenlegi eszközkészletből hiányzik.

### Tag ID → handler tábla

| TAG ID | Handler cím | Valószínű név | Konfidencia |
|---|---|---|---|
| — | `0x002dcb48` | `gameswf_stream_open_tag` (nem loader, a *diszpécser maga*) | **Konfirmált** (string+dekompilálás) |
| — | `0x002dcc34` | `gameswf_stream_close_tag` (ua.) | **Konfirmált** |
| 72 (`DoABC`) | `0x002c0828` | `gameswf_do_abc_loader` | **Konfirmált** (string: "doABC tag loader") |
| 82 (`DoABC2`, névtelen ABC) | *(gameswf-ben ugyanaz a loader-fv., mint a 72-nél — cím itt nem külön ellenőrzött)* | valószínűleg `gameswf_do_abc_loader` | Gyanított, nincs külön bizonyíték |
| 2/22/32/46/83 (`DefineShape`/`2`/`3`/`4`) | `0x002c37f8` (gyanú) | shape/edge-record olvasó | **Gyanított, kontrollfolyam-alapú, nem konfirmált** |
| 11/33 (`DefineText`/`2`) | `0x002c37f8` (ua., a `tag_type==0xb` ág) | glyph/edge-record olvasó | **Gyanított, kontrollfolyam-alapú** |
| 39 (`DefineSprite`) | — | `gameswf_sprite_definition_read_tags` a *beágyazott tag-listát* dolgozza fel, de a `DefineSprite` **loader magát** (ami a `sprite_definition`-t létrehozza és a fenti függvényt meghívja) nem azonosítottam külön | Nyitott |
| 12 (`DoAction`) | `0x2bf8e0` körüli kód, de **Ghidra nem tart ott önálló függvényt** — a legközelebbi definiált függvény (`FUN_002bf7b0`) egy karakter/cél-string kereső logikát tartalmaz, ami inkább `SetTarget`-re vagy egy `find_target`-jellegű segédfüggvényre hasonlít, mint magára a `DoAction` loaderre | Nyitott |
| 15 (`StartSound`) | `0x2c3638` körüli kód, **szintén nincs önálló függvény-határ** ott; a legközelebbi definiált függvények (`FUN_002c325c`, `FUN_002c37f8`) dekompilálva nem sound-loader logikát mutatnak | Nyitott |
| 1 (`ShowFrame`), 4/26 (`PlaceObject`/`2`), 5/28 (`RemoveObject`/`2`), 6/21/35/90 (`DefineBits*`), 20/36 (`DefineBitsLossless*`), 37 (`DefineEditText`), 8/48/75 (`DefineFont*`), 34 (`DefineButton2`), 14 (`DefineSound`), 59 (`DoInitAction`), 69 (`FileAttributes`) | — | — | **Nem található ebben a menetben** |

**Megállapítás a Ghidra function-boundary anomáliáról:** több tag-loader gyanús string-xrefje (`0x2bf8e0`, `0x2c3638`) olyan címre esik, ahol a Ghidra automatikus elemzése *nem* hozott létre önálló függvényt — vagy azért, mert a kód valójában egy nagyobb, már meglévő függvény belseje (amit a jelenlegi `get_function_by_address` hívás valamiért mégsem talált meg — ez maga is gyanús), vagy mert az elemzés hiányos ezen a szakaszon. Javaslat: a Ghidra GUI-ban kézzel "Disassemble"/"Create Function" ezekre a címekre, utána az MCP-vel dekompilálható lesz.

---

## TASK 4 — AVM1 (ActionScript 2 VM)

**A futtatókörnyezet egyértelműen AVM1-alapú (ActionScript 2), és emellett AS3/ABC (AVM2) támogatást is tartalmaz** — ez utóbbit a korábbi menetben azonosított `gameswf_do_abc_loader` (DoABC tag) bizonyítja önmagában. Tehát **mindkettő jelen van** (ez megfelel a gameswf azon ismert kiterjesztésének, amely a natív AS3/ABC futtatáshoz egy külön "abc" alrendszert integrál a klasszikus AVM1 mellé — ismert pl. a Scaleform/egyes portokból, vagy a gameswf saját `gameswf_abc.cpp`/`gameswf_avm2.cpp` moduljaiból).

**Konfirmált, teljes AS2 built-in metódus-lista** (`avm1_builtin_prototypes_init`, 0x2d228c — l. TASK 1/2):

- **Object.prototype:** `addProperty`, `registerClass`, `hasOwnProperty`, `watch`, `unwatch`, `addEventListener`
- **String.prototype:** `toString`, `valueOf`, `fromCharCode`, `charCodeAt`, `concat`, `indexOf`, `lastIndexOf`, `slice`, `split`, `substring`, `substr`, `toLowerCase`, `toUpperCase`, `charAt`, `length`
- **MovieClip.prototype:** `gotoAndStop`, `gotoAndPlay`, `nextFrame`, `prevFrame`, `getBytesLoaded`, `getBytesTotal`, `swapDepths`, `duplicateMovieClip`, `getDepth`, `createEmptyMovieClip`, `removeMovieClip`, `hitTest`, `startDrag`, `stopDrag`, `loadMovie`, `unloadMovie`, `getNextHighestDepth`, `createTextField`, `attachMovie`, `localToGlobal`, `globalToLocal`, `getRect`, `getBounds`, `setMask`, `beginFill`, `endFill`, `lineTo`, `moveTo`, `curveTo`, `clear`, `lineStyle`, `setFPS`, `getPlayState`, `addFrameScript`

Ez azt jelenti, hogy a runtime a **Drawing API-t is támogatja** (`beginFill`/`lineTo`/`moveTo`/`curveTo`/`clear`/`lineStyle`) — vagyis futásidőben dinamikusan rajzolt vektorgrafikát is le tud kezelni, nem csak a fordított SWF tartalmát.

### Bájtkód-opcode diszpécser — méret-anomália

A `"unknown opcode 0x%02X"` (0x2ba9fc, `FUN_002ae784` hivatkozza) és a `"TODO opcode 0x%02X"` (0x2ae570, `FUN_002ab2d0` hivatkozza) stringek alapján az AVM1 bájtkód-végrehajtó valahol a `0x2ab2d0`–`0x2ba9fc` tartományban van. **Viszont** a Ghidra `get_function_by_address(0x2ab2d0)` egy **~1 MB méretű "függvényt"** ad vissza (`Body: 002ab2d0 - 003ae4bb`), ami tartalmazza — ellentmondásosan — azokat a címeket is, amelyeket korábban *önálló, más nevű függvényként* sikerült dekompilálni (pl. `0x2b45b8`, `0x2bc4b0`, `0x2c0828`, `0x2c189c`, `0x2d228c`, `0x2dcb48`). Ez **egyértelműen a Ghidra automatikus elemzésének hibája/inkonzisztenciája**, nem valódi 1 MB-os függvény. A dekompilálási kísérlet emiatt (is) időtúllépésbe futott (a szerver 5 másodperces timeout-ja).

**Következtetés:** az AVM1 opcode-diszpécser (a klasszikus `Push`/`Pop`/`GetVariable`/`SetVariable`/`GetMember`/`SetMember`/`CallFunction`/`CallMethod`/`DefineFunction`/`DefineFunction2`/`Jump`/`If`/`GotoFrame`/`SetTarget` stb. `switch`-ága) **valahol ezen a területen van**, de a pontos függvényhatárait **ebben a menetben nem sikerült megbízhatóan lehatárolni** a fenti Ghidra-anomália miatt. Javasolt következő lépés: a Ghidra GUI-ban manuálisan újraelemezni (`Clear Code Bytes` + `Create Function`) a `0x2ab2d0` körüli területet, hogy az automatikus elemzés helyesen szét tudja választani az egymásba csúszott függvényeket.

---

## TASK 5 — ExternalInterface

**Ez a legfontosabb és legjobban dokumentált eredmény ebben a menetben.**

A `as_global_externalinterface_init` (0x27d584) függvény pontosan felépíti az AS2 `flash.external.ExternalInterface` objektumot:

```cpp
// eredeti: FUN_0027d584 -> as_global_externalinterface_init
as_object* init_external_interface(as_object* global, ...)
{
    as_object* ei = new_as_object(...);

    // global.flash.external.ExternalInterface = ei
    as_object* flash_ns    = get_or_create_member(global, "flash");
    as_object* external_ns = get_or_create_member(flash_ns, "external");
    external_ns->set_member("ExternalInterface", ei);   // "external" (0027d780)
    ei->set_member("ExternalInterface", ei);              // önhivatkozás (0027d78c)

    // natív C++ függvénykötések:
    ei->set_member("addCallback",    wrap_native(NATIVE_FN_1 /* DAT_0027d7a0 */));
    ei->set_member("removeCallback", wrap_native(NATIVE_FN_2 /* DAT_0027d7b0 */));
    ei->set_member("call",           wrap_native(NATIVE_FN_3 /* DAT_0027d7c4 */));  // (a 3. tag "call"-nak felel meg pozíció szerint)

    ei->flags |= INITIALIZED;   // *(piVar1+0x1b)=1
    return ei;
}
```

`wrap_native()` (a dekompilátumban `FUN_001c9f64`) egy natív C-függvénymutatót csomagol `as_value`-vá (ez a gameswf `as_c_function_ptr` / natív-metódus regisztrációs mintája) — vagyis az ActionScriptből `ExternalInterface.addCallback("nev", this, fuggveny)` hívás **közvetlenül egy C++ (natív 3DS-oldali) függvényt** hív meg, nem egy másik ActionScript-réteget.

**Amit NEM sikerült feloldani:** a `DAT_0027d7a0`, `DAT_0027d7b0`, `DAT_0027d7c4` literál-pool szavakban tárolt tényleges natív függvénycímeket — ehhez nyers adatolvasás (memória-dump vagy a Ghidra GUI "Data → Pointer" konverziója) szükséges, amit a jelenlegi eszközkészlet nem tesz lehetővé. **Ez a legfontosabb következő lépés** — ha ez a három cím feloldódik, onnantól dekompilálhatók a natív `addCallback`/`removeCallback`/`call` implementációk, és pontosan látni fogjuk, **hogyan lép át egy ActionScript-hívás a 3DS natív alkalmazásba** (feltehetően egy név→C-függvénymutató regisztrációs táblán és egy natív dispatch-hívási konvención — pl. AS `Array`/paraméterek C++ `as_value` listává alakításán — keresztül).

---

## TASK 6 — Renderer

A `RASTER_IMAGE` (0x3cbb88) és `SPRITE_RENDERER` (0x3cbbc8) stringekre **nem található közvetlen code-xref** ebben a menetben — ezek valószínűleg egy típus-név ↔ enum megfeleltető táblában szerepelnek (hasonlóan a korábbi menetben talált `SPRITE_OBJECT_MOUSE_STATE_Up/Over/Down` mintához), amit egy még nem azonosított "objektum-regisztry" olvas fel indirekt (pl. hash-alapú string-összehasonlítás) módon, nem közvetlen immediate-load formában — ezért a sztenderd xref-keresés nem találja meg a hivatkozó kódot.

A korábbi menetben már azonosított és e menetben megerősített elemek:
- `shift3ds_gameswf_renderer_interface_init` (0x13da00) köti be a Mojito motor render-alrendszerét a gameswf-be (`interface_gameswf_renderer` erőforrás-útvonal alapján).
- `gameswf_button_register_mouse_state_names` (0x27eed8) igazolja a gomb-objektum (`button_character_instance`) Up/Over/Down hit-test állapotgépét.
- Erős, kontrollfolyam-alapú jelölt a shape-rendereléshez: `FUN_002c37f8` (ld. TASK 2/3) — ez a klasszikus SWF `SHAPERECORD`/edge-record bitfolyam-olvasó, amely a `DefineShape`-családot és a `DefineText` glyph-körvonalait is kiszolgálja.

**Transzformációs mátrixok, clipping, framebuffer-célválasztás (top/bottom 3DS képernyő), bitmap-kezelés:** ebben a menetben nem volt idő/bizonyíték ezekre — nyitott pont a következő menetre.

---

## TASK 7 — Audio

Amit a korábbi és e menetből tudunk (string-szintű, függvény-szintű megerősítés még hiányzik):

- `"define stream sound: format=%d, rate=%d, 16=%d, stereo=%d, ct=%d\n"` (0x3cbe58) és `"define sound: ch=%d, format=%d, rate=%d, 16=%d, stereo=%d, ct=%d\n"` (0x3cbfec) → a `DefineSound`/`DefineSoundStream` tag-betöltők formátum-jelzőket naplóznak (formátum-kódolás, mintavételi ráta, bit-mélység, mono/sztereó, minta-szám) — ez megegyezik a klasszikus SWF hangformátum-fejléccel (PCM/ADPCM/MP3/Nellymoser kódolás-azonosítókkal).
- `"AUDIO_SOUND_MEMORY_POLICY_InMemory / PartiallyInMemory / Streamed"` — a 3DS-natív Mojito audio-alrendszer irányelvei, amikhez a `DefineSoundStream` van kötve.
- `"attachSound"`, `"loadSound"`, `"onSoundComplete"`, `"_soundbuftime"` — AS2 `Sound` objektum natív metódusai/eseményei (feltehetően az `avm1_builtin_prototypes_init`-hez hasonló mintában regisztrálva, de a `Sound.prototype`-ot építő függvényt ebben a menetben nem azonosítottam külön — csak a `MovieClip`/`Object`/`String` prototípusokat).

**A pontos `StartSound`/`Sound_Create`/`Sound_Start`/`Sound_Stop`/`Sound_StopAll` függvénycímek ebben a menetben nem lokalizálhatók** — a string-xref-ek olyan címekre esnek, ahol a Ghidra function-boundary elemzése (ld. TASK 3/4 anomália) nem ad megbízható határt.

---

## TASK 8 — Input

Megerősítve (korábbi + e menet):

- **Egér/érintés:** `_xmouse`/`_ymouse`, `onMouseDown`/`onMouseUp`/`onMouseMove`, `MouseEvent`, `INPUT_MOUSE_BUTTON_Primary/Secondary/Middle` — a 3DS touch screen a Flash "egér" API-ra van rákötve (ismert minta point-and-click Flash-portoknál).
- **Gomb-állapotgép:** `gameswf_button_register_mouse_state_names` (0x27eed8) — Up/Over/Down.
- **Billentyűzet:** `GetKeyboard`, `Keyboard`, `ACTION_DEVICE_Keyboard`, `INPUT_KEYBOARD` stringek megvannak, és két `"isDown"` string is található (`0x1067c0`, `0x2e1408`) — ez összhangban van az AS2 `Key.isDown()` natív implementációjával, **de a konkrét `Key.isDown` metódust regisztráló/kiszolgáló függvényt ebben a menetben nem sikerült dekompilálással megerősíteni** (a stringekhez tartozó xref-lánc követése egy következő menet feladata).

**`Key.isDown()` pontos implementációja, natív 3DS input → gameswf Key-objektum leképezés:** nyitott pont.

---

## TASK 9 — Rekonstrukciós terv

### 9.1 SWF runtime architektúra (eddig igazolt vázlat)

A motor a nyílt forráskódú **gameswf** könyvtár egy 3DS-re portolt, AS3/ABC (`do_abc_loader`) képességgel bővített változata. A `SWF_Load → SWF_ParseHeader → SWF_ParseTags (loop) → [tag loader dispatch]` lánc a `gameswf_create_movie_instance` → `gameswf_movie_def_impl_read_header` → `gameswf_movie_def_impl_read_tags` függvényhármason keresztül konkrétan bejárható és dekompilálva van.

### 9.2 Függvény-függőségi gráf
Ld. TASK 2.

### 9.3 Tag handler tábla
Ld. TASK 3 (részleges — a hash-tábla tartalma még nincs kiolvasva).

### 9.4 AVM1 opcode-implementációs tábla
**Nem áll rendelkezésre** — az opcode-diszpécser függvényhatárai jelenleg megbízhatatlanok a Ghidra elemzésében (ld. TASK 4). Amit tudunk: a beépített (natív-kötésű, nem bájtkód-opcode) AS2 API-felület teljes listája megvan (ld. TASK 4 táblázat).

### 9.5 ExternalInterface architektúra
Ld. TASK 5 — a legjobban dokumentált alrendszer ebben a menetben.

### 9.6 Renderer architektúra
Részleges — ld. TASK 6.

### 9.7 Audio architektúra
Részleges — ld. TASK 7.

### 9.8 Input architektúra
Részleges — ld. TASK 8.

### 9.9 Egy minimális, önálló Flash runtime-hoz szükséges függvények (jelenlegi ismeret szerint)

Ezek azok a függvények, amelyek **motor-magnak** (nem játék-specifikusnak) minősülnek, és eddig konkrétan azonosítva/dekompilálva vannak:

- `gameswf_create_movie_instance` (0x2dc0b4)
- `gameswf_movie_def_impl_read_header` (0x2b45b8)
- `gameswf_movie_def_impl_read_tags` (0x2b4b8c)
- `gameswf_sprite_definition_read_tags` (0x2c189c)
- `gameswf_stream_open_tag` / `gameswf_stream_close_tag` (0x2dcb48 / 0x2dcc34)
- `gameswf_do_abc_loader` (0x2c0828)
- `as_global_externalinterface_init` (0x27d584)
- `avm1_builtin_prototypes_init` (0x2d228c)
- `gameswf_button_register_mouse_state_names` (0x27eed8)
- (jelölt, nem konfirmált) `FUN_002c37f8` — shape/edge-record olvasó

Ezen felül **szükséges, de még nem lokalizált**: a tag-loader hash-tábla regisztrációja, az AVM1 opcode-diszpécser, a konkrét hang-dekódolók, a renderer mátrix/display-list kód, a natív ExternalInterface/Key.isDown implementációk.

### 9.10 Feltehetően csak Shift-DX-specifikus (játék-logika, NEM motor-mag)

Ebben a menetben nem találtam olyan függvényt, amit egyértelműen "csak Shift-DX játéklogikaként" (pl. pálya-/fizika-kód natív oldalon) tudnék azonosítani — ez arra utal, hogy **a tényleges játéklogika maga SWF/ActionScript-tartalomként fut a gameswf motorban**, a natív 3DS-kód nagyrészt csak a motor portolásáért (renderer, audio, input, fájl-I/O) felelős. Ez jó hír a cél szempontjából (önálló Flash runtime): minél kevesebb natív kódtól kell "leválasztani" a motort, mert a játék maga már a beágyazott SWF-adatban van.

---

## Összefoglaló táblázat: ebben a menetben átnevezett/kommentált Ghidra-szimbólumok

| Cím | Új név | Bizonyíték típusa |
|---|---|---|
| `0x002dc0b4` | `gameswf_create_movie_instance` | string + dekompilálás |
| `0x002b45b8` | `gameswf_movie_def_impl_read_header` | string + dekompilálás |
| `0x0027d584` | `as_global_externalinterface_init` | string + dekompilálás |
| `0x002d228c` | `avm1_builtin_prototypes_init` | string + dekompilálás (55 metódusnév egyezés) |

(A korábbi menetből: `shift3ds_gameswf_renderer_interface_init`, `gameswf_stream_open_tag`, `gameswf_stream_close_tag`, `gameswf_movie_def_impl_read_tags`, `gameswf_sprite_definition_read_tags`, `gameswf_do_abc_loader`, `gameswf_button_register_mouse_state_names` — összesen 11 megerősített, átnevezett függvény eddig.)

**Nem módosítottam és nem töröltem semmilyen bináris adatot** — kizárólag Ghidra-szimbólumneveket és dekompilátor-kommenteket írtam, ahogy kérted.

## Legfontosabb javasolt következő lépés

Egy **nyers adat-/memóriaolvasó** képesség hozzáadása az MCP eszközkészlethez (akár csak "adott cím, N bájt kiolvasása" szinten) azonnal feloldaná a jelenlegi legnagyobb szűk keresztmetszetet: a `DAT_...` literál-pool szavakban tárolt natív függvénymutatók (ExternalInterface addCallback/removeCallback/call, ~55 AVM1 built-in metódus, tag-loader hash-tábla tartalma) értékét. Ezek nélkül ezek a pontok csak *szerkezetileg* igazolhatók, *címre* nem.
