# Shift2_3ds — a Ghidra-alapú olvasható forráskód-visszaépítés a beágyazott SWF/Flash motorról

**Bináris:** Nintendo 3DS "Shift2_3ds" (Mojito engine), ARM, ~4,5 MB kód/adat (`ram: 0x00000000–0x0044ffff`)
**Elemzett funkciók száma a bináris egészében:** 10 486 (ebből eredetileg csak ~8 volt névvel ellátva — a maradék `FUN_xxxxxxxx` / `switchD_xxxxxxxx`)
**Dátum:** 2026-08-17

---

## 1. Mit találtunk — azonosítás

A stringek és a hívási minták alapján a bináris a **[gameswf](https://github.com/tulrich/gameswf)** nyílt forráskódú SWF/Flash-lejátszó könyvtár egy módosított, 3DS-re portolt példányát tartalmazza. A "Shift" eredetileg Flash (SWF) játék volt (Armor Games) — a 3DS-es "Shift 2" port nem újraírta a játéklogikát naprakész natív kódban, hanem **belefordította a gameswf könyvtárat**, és azzal futtatja le magát az eredeti SWF-tartalmat (`mojito_data.zip:/DATA/`, `Shift2_3ds`, `shift2_3ds` stringek).

Ezt az alábbi, a gameswf forráskódjából szó szerint felismerhető log-/hibaüzenet-stringek erősítik meg egyértelműen:

| Cím | String | gameswf megfelelő |
|---|---|---|
| `0x2dcc04` | `"---------------tag type = %d, tag length = %d\n"` | `movie_def_impl::read()` tag-ciklus napló |
| `0x2b4e44`, `0x2c19f0` | `"*** no tag loader for type %d\n"` | ismeretlen SWF tag figyelmeztetés |
| `0x2bc650` | `"sprite::add_display_object(): unknown cid = %d\n"` | `sprite_instance::add_display_object()` |
| `0x2bd1c8` | `"sprite::replace_display_object(): unknown cid = %d\n"` | `sprite_instance::replace_display_object()` |
| `0x2bfa28` | `"tag %d: do_action_loader\n"` | AS2 `DoAction` tag betöltő |
| `0x2c0904` | `"\n doABC tag loader, abc_name = '%s'\n"` | AS3 `DoABC` tag betöltő |
| `0x2ce4d8`/`0x2ce4fc` | `"tag %d: do_init_action_loader\n"` / `"init actions for sprite %d\n"` | `DoInitAction` |
| `0x3cbe58` | `"define stream sound: format=%d, rate=%d, 16=%d, stereo=%d, ct=%d\n"` | `DefineSoundStream` betöltő |
| `0x3cbfec` | `"define sound: ch=%d, format=%d, rate=%d, 16=%d, stereo=%d, ct=%d\n"` | `DefineSound` betöltő |
| `0x2c3668` | `"start_sound tag: id=%d, stop = %d, loop ct = %d\n"` | `StartSound` betöltő |
| `0x2d7354` | `"DefineFont%d tag\n"` | `DefineFont`/`DefineFont2`/`DefineFont3` |

A "renderer" réteget a 3DS-specifikus kód köti be — ezt a `0x13da00` címen lévő inicializáló függvény bizonyítja, amely közvetlenül hivatkozik az `"interface_gameswf_renderer"` és `"CODE/MEDIA/COMMON/INTERFACE/GAMESWF/"` erőforrás-útvonalakra. Ezt átneveztem `shift3ds_gameswf_renderer_interface_init`-re.

---

## 2. Modultérkép (cím-tartományok)

| Terület (kb.) | Alrendszer | Megjegyzés |
|---|---|---|
| `0x000000–0x000020` | ARM kivétel-vektortábla | `Reset`, `IRQ`, `FIQ` stb. — eredetileg is névvel ellátva volt |
| `~0x0013d000–0x0013e000` | Mojito↔gameswf render-interfész kötés | `shift3ds_gameswf_renderer_interface_init` |
| `~0x0027e000–0x0027f000` | gameswf gomb (button) állapotgép | Up/Over/Down egér-állapot regisztráció |
| `~0x0029d000–0x002e0000` | **gameswf tag-betöltők + AS2/AS3 motor** | tag-diszpécser hurok, DoAction/DoABC, sprite/display-list |
| `~0x0019f000–0x00194000`, `~0x00222000–0x00224000` | gameswf `base/` réteg | stream I/O, memóriakezelés, hash-tábla, refcount (smart_ptr/weak_ptr minta) |
| `~0x003ca000–0x003cc000` | Hangalrendszer (DefineSound / DefineSoundStream betöltők) | 3DS audio HW-hez kötve (`AUDIO_SOUND_MEMORY_POLICY_*`) |
| `~0x00190000`, `~0x00333000`, `~0x002bc000` | Egér/érintőképernyő → ActionScript esemény leképezés | `_xmouse`, `_ymouse`, `onMouseDown/Up/Move`, `MouseEvent` |

A gameswf mag könyvtár (tag-parsolás, sprite/display-list, ActionScript VM, betűtípus/kép dekódolás) **hozzávetőleg 300–500 függvényt** tesz ki a 10 486-ból. Ennek 100%-os, tétel szintű visszafejtése (minden segédfüggvény, minden tag-betöltő egyenkénti dekompilálása) embernapokat/heteket igényelne — ez a dokumentum a **motor gerincét** (a tag-olvasó ciklust, a stream-formátumot, valamint a hang- és inputkötést) építi vissza olvasható, kommentált formában, konkrét, dekompilált és ellenőrzött kód alapján.

---

## 3. A motor magja — visszaépített, olvasható kód

Az alábbi kód a Ghidra dekompilátor kimenetéből lett kézzel tisztítva: értelmes nevekre cserélve a `param_N`/`uVar` jellegű azonosítókat, és a gameswf ismert szerkezetéhez igazítva (ez utóbbi a nyílt forráskódú gameswf projekt saját elnevezési konvencióját követi, mivel a logika 1:1 megegyezik vele).

### 3.1. `gameswf_stream::open_tag` — cím `0x0002dcb48`

SWF tag-fejléc olvasása: 16 bites szó, felső 10 bit = tag típus, alsó 6 bit = hossz (vagy ha az 0x3F, egy külön 32 bites kiterjesztett hossz következik). A tag végét egy verembe (`m_tag_stack`) toljuk, hogy a `close_tag()` ellenőrizni tudja a pozíciót.

```cpp
// eredeti: FUN_002dcb48  ->  gameswf_stream_open_tag (átnevezve Ghidrában)
uint16_t stream::open_tag(stream* s)
{
    s->m_record_scoping = false;
    s->m_scoped        = false;

    uint16_t tag_header = s->read_u16();      // (*pcVar5)(&local_18, 2)
    uint16_t tag_type   = tag_header >> 6;
    uint32_t tag_length = tag_header & 0x3F;

    if (tag_length == 0x3F) {
        tag_length = s->read_u32();            // hosszú tag: külön 32 bites hossz
    }

    if (s->verbose_parse()) {                  // FUN_002230ac() -> globális verbose-flag
        // (naplózás — ld. "tag type = %d, tag length = %d")
    }

    uint32_t tag_end = s->get_position() + tag_length;
    s->m_tag_stack.push_back(tag_end);          // FUN_0019ef40 -> vektor push_back
    return tag_type;
}
```

### 3.2. `gameswf_stream::close_tag` — cím `0x0002dcc34`

```cpp
// eredeti: FUN_002dcc34 -> gameswf_stream_close_tag
void stream::close_tag(stream* s)
{
    uint32_t expected_end = s->m_tag_stack.back();
    s->m_tag_stack.pop_back();

    if (s->get_position() != expected_end) {
        // hossz-eltérés — gameswf ilyenkor figyelmeztetést naplóz és a végére ugrik
        log_error("tag length mismatch");
    }
    s->set_position(expected_end);
    s->m_record_scoping = false;
}
```

### 3.3. Fő tag-diszpécser hurok — `movie_def_impl::read()` megfelelője, cím `0x0002b4b8c`

Ez a legfelső szintű ciklus, amely egy egész SWF-fájl (vagy beágyazott adatfolyam) összes tagjét feldolgozza: kiolvassa a fejlécet, megkeresi a hozzá tartozó betöltő függvényt egy `(tag_type -> loader_fn)` hash-táblában (`s_tag_loaders`, a dekompilátumban `DAT_002b4e40`), majd meghívja azt.

```cpp
// eredeti: FUN_002b4b8c -> gameswf_movie_def_impl_read_tags
void movie_def_impl::read_tags(movie_def_impl* m, stream* in)
{
    hash<int,int,loader_function>* tag_loaders = get_tag_loaders(); // DAT_002b4e40

    while (in->get_position() < in->get_tag_end_position() && !m->m_abort_flag)
    {
        uint16_t tag_type = stream::open_tag(in);

        if (tag_type == TAG_END /* 0 */) {
            m->m_loading_frame++;
            std::pair<bool,int> result = m->on_end_tag();
            if (result.first) {
                m->m_root->notify(); // képkocka-vég értesítés
            }
        }
        else {
            loader_function loader = nullptr;
            if (tag_loaders != nullptr &&
                tag_loaders->get(tag_type, &loader) && loader != nullptr)
            {
                loader(in, tag_type, m);
            }
            else {
                // "*** no tag loader for type %d\n" — ismeretlen tag, kihagyjuk
                log_error("no tag loader for type %d", tag_type);
            }
        }

        stream::close_tag(in);

        if (tag_type == TAG_END &&
            in->get_position() == in->get_tag_end_position()) {
            break;
        }
    }

    // takarítás: renderer, karakter-cache, importok, exportok felszabadítása
    m->cleanup();
}
```

### 3.4. Beágyazott (movieclip/sprite) tag-hurok — `sprite_definition::read()`, cím `0x0002c189c`

Szinte azonos a fentivel, de egy `DefineSprite` tag *belsejét* dolgozza fel — saját, a szülő adatfolyamon belüli hossz-korláttal, és a végén `TAG_SHOWFRAME`/`TAG_END` helyett a sprite saját képkocka-számlálóját (`m_loading_frame`) növeli.

```cpp
// eredeti: FUN_002c189c -> gameswf_sprite_definition_read_tags
void sprite_definition::read_tags(sprite_definition* spr, stream* in, movie_def_impl* m)
{
    uint32_t sprite_tag_end = in->get_tag_end_position();

    spr->m_frame_count = in->read_u16();
    if (spr->m_frame_count < 2) spr->m_frame_count = 1;

    spr->m_playlist.resize(m->frame_count());   // (**...)(param_1) -> movie frame count

    while (in->get_position() < sprite_tag_end && !spr->m_finished_loading)
    {
        uint16_t tag_type = stream::open_tag(in);

        if (tag_type == TAG_END) {
            spr->m_loading_frame++;
            spr->m_root->notify();
        }
        else {
            loader_function loader = nullptr;
            if (get_tag_loader(tag_type, &loader)) {   // FUN_002b35d8
                loader(in, tag_type, spr);
            }
        }

        stream::close_tag(in);
    }
}
```

### 3.5. AS3 `DoABC` tag betöltő — cím `0x0002c0828`

```cpp
// eredeti: FUN_002c0828 -> gameswf_do_abc_loader
void do_abc_loader(stream* in, uint16_t tag_type, movie_def_impl* m)
{
    IF_VERBOSE_PARSE(log_msg("\n doABC tag loader, abc_name = '%s'\n", ...));

    uint32_t flags   = in->read_u32();
    tu_string name   = in->read_string();

    abc_def* abc = new abc_def();
    if (abc && abc->read(in)) {
        m->add_abc_block(abc);           // regisztráljuk a mozifájl ABC-blokkjai közé
    }
}
```

*(A `DoAction` (AS2) betöltő címe stringxreffel `0x0002bf8e0` körül azonosítható, de a hozzá tartozó pontos függvényhatár ebben a menetben nem volt egyértelműen lehatárolható — ld. 6. pont, "Nyitott kérdések".)*

---

## 4. Hangalrendszer

A hang-tag betöltők (`DefineSound`, `DefineSoundStream`, `StartSound`) naplóüzenetei alapján a `~0x003ca000–0x003cc000` és `~0x002aa000/0x002c3000/0x002d5000` területeken vannak — ezek a gameswf `gameswf_sound.cpp`-jének, illetve annak 3DS audio HW-re kötött verziójának felelnek meg. Konkrét bizonyíték a natív audio-rendszerre való kötésre:

- `"AUDIO_SOUND_MEMORY_POLICY_InMemory / PartiallyInMemory / Streamed"` — a 3DS saját streamelt-audio API-jának irányelvei, amikhez a gameswf `DefineSoundStream` tagjait kötik.
- `"invalid sound sample id: %d\n"`, `"can't find sound '%s'\n"` — gameswf `sound_sample` hibakezelés.
- ActionScript oldali kapcsolódás: `"attachSound"`, `"loadSound"`, `"onSoundComplete"`, `"_soundbuftime"` — a gameswf `Sound` ActionScript objektum natív metódusai.

**Ebben a menetben a konkrét tag-betöltő függvényeket még nem sikerült 100%-osan lehatárolni** (a stringekre mutató néhány code-xref olyan címre esett, amit a Ghidra function-boundary elemzése nem tart önálló függvénynek — valószínűleg egy nagyobb, még fel nem bontott switch-blokk belseje). Ez jó jelölt egy következő menetre.

---

## 5. Input: érintőképernyő → gameswf egér-esemény

A 3DS-es port a **touch screen-t Flash "egér"-ként** emulálja (ahogy a legtöbb point-and-click Flash-portnál szokás):

- `_xmouse`, `_ymouse` (ActionScript beépített property-k)
- `onMouseDown`, `onMouseUp`, `onMouseMove`, `MouseEvent` (AS2/AS3 esemény-callback nevek)
- `INPUT_MOUSE_BUTTON_Primary/Secondary/Middle` — Mojito natív input-absztrakció, ami a touch-eseményt "elsődleges gombként" adja át
- `SPRITE_OBJECT_MOUSE_STATE_Up/Over/Down` — a gameswf gomb- (button-) objektum három állapota

A `0x0027eed8` című függvény (átnevezve `gameswf_button_register_mouse_state_names`) ezt konkrétan igazolja — dekompilálva:

```cpp
// eredeti: FUN_0027eed8
void register_button_mouse_state_names(object_registry* reg)
{
    reg->add("SPRITE_OBJECT_MOUSE_STATE_Up",   BUTTON_STATE_UP);
    reg->add("SPRITE_OBJECT_MOUSE_STATE_Over", BUTTON_STATE_OVER);
    reg->add("SPRITE_OBJECT_MOUSE_STATE_Down", BUTTON_STATE_DOWN);
}
```

Vagyis az érintés → Mojito `INPUT_MOUSE` → gameswf `sprite_instance`/`button_character_instance` hit-test → `SPRITE_OBJECT_MOUSE_STATE` → ActionScript `onMouseDown/Up/Move` / `MouseEvent` lánc rekonstruálható a fenti kapcsolódási pontok mentén.

---

## 6. Amit a Ghidra-projektben elmentettem

A következő függvényeket **véglegesen átneveztem** a Ghidra projektben (ez a "meglévő adat" a jövőbeli menetek számára is megmarad), és két helyre dekompilátor-kommentet is fűztem:

| Cím | Új név |
|---|---|
| `0x0013da00` | `shift3ds_gameswf_renderer_interface_init` |
| `0x0002dcb48` | `gameswf_stream_open_tag` |
| `0x0002dcc34` | `gameswf_stream_close_tag` |
| `0x0002b4b8c` | `gameswf_movie_def_impl_read_tags` |
| `0x0002c189c` | `gameswf_sprite_definition_read_tags` |
| `0x0002c0828` | `gameswf_do_abc_loader` |
| `0x0027eed8` | `gameswf_button_register_mouse_state_names` |

## 7. Nyitott kérdések / folytatási pontok

- **DoAction (AS2) betöltő pontos határa** — a string-xref címe (`0x2bf8e0`) egy olyan blokkra esik, amit a Ghidra jelenleg nem tart önálló függvénynek; érdemes manuálisan (Ghidra GUI-ban "Create Function") kijelölni, utána dekompilálni.
- **`s_tag_loaders` hash-tábla tartalmának kinyerése** — ha sikerülne a `DAT_002b4e40` mögötti hash-tábla bucket-tömbjét kiolvasni (`list_data_items` / nyers memória-dump), abból *az összes* támogatott SWF tag-típus → betöltő-függvény párost meg lehetne kapni egy csapásra, ami a leggyorsabb út a teljes motor feltérképezéséhez.
- **Konkrét hang-dekódoló (ADPCM/MP3) függvények** azonosítása és dekompilálása.
- **ActionScript VM (AVM1/AVM2) opcode-diszpécser** megkeresése — valószínűleg az egyik nagy, `switchD_...` néven látszó ugrótábla mögött van; ehhez érdemes a `case`-ágak konstansait összevetni az SWF ActionScript opcode-listával.

---

*Ez a dokumentum egy első, de érdemi visszaépítési menet eredménye. A gameswf motor magja (tag-stream, fő ciklusok, ABC-betöltő, gomb/egér-állapotgép) ellenőrzött, dekompilált kód alapján lett rekonstruálva. A hang- és ActionScript-VM alrendszerek egyelőre csak string-/hívási-lánc szinten vannak azonosítva — ezek mélyebb visszaépítése egy következő menetben folytatható.*
