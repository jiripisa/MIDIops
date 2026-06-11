# Hloubkový audit MIDIops — 2 kritické, 10 důležitých, 21 drobných nálezů

Audit pokryl sedm dimenzí (engines, concurrency, MIDI, shell, architektura, embedded, testy+dokumentace). Každý nález byl nezávisle verifikován čtením kódu a u klíčových defektů spustitelnou reprodukcí proti reálným zdrojům z `core/`. Žádný kandidátní nález nebyl vyvrácen. Duplicitní nálezy napříč dimenzemi (stejný kořenový defekt nalezený více agenty) jsou níže sloučeny.

## Celkový stav

Jádro je v dobré kondici: split `core/` vs. `platform/` je skutečně čistý (žádné platformní hlavičky v `core/`), `Scale`, `ClockFollower` a generátory Berlin módu zvládají hraniční případy korektně, hostová (SDL/RtMidi) strana souběžnosti je disciplinovaná a testy enginů jsou silné (140/140 prochází). Tři systémové slabiny ale procházejí napříč: (1) **stuck-note cesty** — `BerlinEngine` nezavírá přehlušenou notu a gate-off obou enginů závisí na hodinách, které externí master může zastavit; (2) **latch kontrakt** — Teensy posílá úrovně přepínačů každý frame, ale tři konzumenti je zpracovávají bez edge-detekce nebo se zastaralým stínovým stavem, což na hardwaru (a jen tam — simulátor posílá hrany, takže defekty maskuje) způsobuje zamrznutí arpu, fantomový MIDI Start a fantomovou regeneraci sekvence; (3) **dokumentační drift** — `HARDWARE.md`, `ASSEMBLY.md` a horní polovina `CLAUDE.md` stále popisují vyřazený chord-trigger prototyp, jehož mrtvý kód (~2 500 řádků + 300 KB bitmap) se navíc dál kompiluje do všech tří buildů. Na Teensy straně je jeden vážný souběhový defekt v zápisu do usbMIDI TX bufferu z ISR.

## Potvrzené nálezy

### Kritické

**N1 — `BerlinEngine::emitStep` neukončí stále znějící notu → trvale visící nota na syntezátoru**
`core/BerlinEngine.cpp:14` (potvrzeno třemi dimenzemi: engines, midi, embedded; reprodukováno spustitelným testem)
`emitStep()` vyšle nový NoteOn a přepíše `soundingNote_`/`gateTicks_` bez NoteOff pro předchozí notu, která je ještě uvnitř svého gate — na rozdíl od `ArpEngine::beginStep`, který přesně tuto pojistku má. `gateTicks` se peče do sekvence při generování z tehdejšího rozlišení, zatímco `stepLenTicks()` čte `params_.resolution` živě. Scénář: v chování Locked/Evolve vygenerovat sekvenci na RESOL=8th s GATE ≥ 59 % (gateTicks 7–11), spustit přehrávání a otočit RESOL na 16th — krok se zkrátí na 6 tiků, stará nota je na hranici kroku stále znějící, její NoteOff se už nikdy nepošle a visící noty se hromadí (repro: 5× NoteOn, 1× NoteOff za jednu otáčku smyčky). `stop()` zavře jen aktuálně sledovanou notu a zařízení nemá žádný all-notes-off, takže není cesta k zotavení. Dosažitelné i přes Generate s Morph < 100 (zachované kroky nesou staré, delší gateTicks).
*Oprava:* na začátku `emitStep()` (případně před advancem v `onClockTick`) vyslat `if (noteSounding_) { emit(false, soundingNote_, 0); noteSounding_ = false; }`; volitelně navíc clampovat `gateTicks_` na aktuální `stepLenTicks()`.

**N2 — usbMIDI TX race: clock ISR i hlavní smyčka zapisují do nechráněného USB-MIDI TX bufferu**
`platform/teensy/TeensyMidiOutput.cpp:22`
`clockIsr()` volá `usbMIDI.sendRealTime()+send_now()` z IntervalTimer ISR, zatímco hlavní smyčka souběžně posílá NoteOn/NoteOff. Ověřeno ve vendorovaném jádře (`cores/teensy4/usb_midi.c`): `usb_midi_write_packed()` mutuje `tx_head`/`tx_available`/kurzor bufferu bez jakéhokoli IRQ guardu a `tx_noautoflush` není reentrantní. Interleaving: ISR přepíše rozepsaný NoteOff paketem Clock a flush vynuluje `tx_available`; obnovené `tx_available -= 4` v hlavní smyčce podteče uint16 na 65532 → další zápis počítá obrovsky záporný offset → **out-of-bounds zápis**; pravděpodobnější mírnější výsledek je tiše ztracený NoteOff (visící nota). Jde o výchozí provozní stav zařízení (interní clock master, ISR 48 Hz při 120 BPM). Komentář v hlavičce „interrupt-safe“ zdrojový kód jádra přímo vyvrací; vedlejší problém: wait-loop ve `write_packed` může v ISR točit `yield()` až 40 ms.
*Oprava:* neposílat z ISR — `clockIsr()` jen inkrementuje `g_clockTicks` a F8 pulzy emitovat z hlavní smyčky; alternativně obalit všechny usbMIDI sendy (a tělo ISR) `noInterrupts()/interrupts()`.

### Důležité

**N3 — Arp swing prodlužuje liché kroky bez zkrácení sudých → kumulativní drift proti MIDI clocku**
`core/ArpEngine.cpp:22`
`stepLenTicks()` přidává `(swing-50)*base/100` tiků každému lichému kroku, ale sudý nikdy nezkrátí — pár kroků trvá `2*base + swing` místo `2*base`, takže pozdě dopadají právě **downbeaty** a zpoždění se pod latch kumuluje donekonečna (rate 1/8, swing 75: 8 kroků = 108 tiků místo 96, tj. celá doba driftu každých ~8 párů proti synchronizovanému drum machine). Vlastní test projektu (`test_arp_engine.cpp:585`) tuto vadnou časovou osu přímo kodifikuje. Sekundárně: celočíselné dělení dělá z knobu SWING no-op pro 17 z 26 pozic při výchozím rate 1/16.
*Oprava:* tempo-neutrální swing — sudé kroky `base + s`, liché `base - s` (pár se sečte na `2*base`); upravit test.

**N4 — Arp Random používá nízké bity LCG mod seqLen → pevná opakující se permutace pro mocniny dvou**
`core/ArpEngine.cpp:278`
`randState_ % seqLen_` z LCG s modulem 2^32: nízké k bity mají periodu přesně 2^k, takže pro seqLen 4/8/16 (nejběžnější hodnoty knobu STEPS) hraje „Random“ navždy identické pořadí not (repro: len=8 → fixní cyklus `0 7 2 1 4 3 6 5`). `randState_` se navíc seeduje jen jednou a nikdy znovu.
*Oprava:* použít horní bity, např. `(randState_ >> 16) % seqLen_`, nebo xorshift jako `BerlinRng`.

**N5 — Zastaralá latch pending nota přežije idle a později „vzkřísí“ notu, kterou uživatel nestiskl**
`core/ArpEngine.cpp:193`
`latchHasPending_` se nečistí, když `beginStep()` přejde do idle na hranici cyklu, ani při fresh-startu bez latch v `noteOn()`. Scénář (reprodukováno proti reálnému enginu): Hold ON, hraje A, stisk B (pending) → Hold OFF (engine idluje, pending zůstává) → později stisk C s Hold OFF a přepnutí Hold ON během cyklu C → na hranici C zastaralá větev nahradí C notou B, která pak smyčkuje navždy, ač v této session nikdy nezazněla.
*Oprava:* čistit `latchHasPending_` při každém přechodu do idle a při každém fresh-startu v `noteOn()` (latch i non-latch větev) — jednořádková změna.

**N6 — Externí Stop (0xFC) se jen přeposílá a zastavení externího clocku nechá notu znít navždy**
`core/app/AppShell.cpp:161`
Oba enginy zavírají gate výhradně v `onClockTick()`; s `clockSource=External` přicházejí tiky jen z příchozích 0xF8. Většina DAW (Ableton) přestane posílat clock při zastavení transportu, takže běžné „stisknout Stop v DAW, zatímco zní nota arpu/Berlinu“ znamená, že NoteOff se nepošle, dokud clock zase neběží. Příchozí `MidiType::Stop` pouze volá `out_->sendStop()` — neflushne noty, nezavolá `onTransport()` aktivního módu ani neaktualizuje `transportState_`.
*Oprava:* při externím Stop (a volitelně při clock-timeoutu v `tick()`) flushnout znějící noty — předat aktivnímu módu `onTransport(Transport::Stop)` / engine panic, aby gate NoteOff odešel okamžitě.

**N7 — Zastaralé `lastLatchOn_` v shellu: fantomový MIDI Start/Stop při opuštění Arp/Berlin a při bootu se zapnutou páčkou**
`core/app/AppShell.cpp:91` (potvrzeno dimenzemi midi i shell)
`onLatch()` se pro capturing módy vrací **před** aktualizací `lastLatchOn_[index]`, zatímco Teensy main posílá úroveň páček každou iteraci smyčky. Scénář: v Arp módu přepnout Latch1 ON (Hold), overlay-switchem přejít do Monitoring — hned další frame shell vidí falešnou „hranu“ a `applyTransport(Play)` pošle nechtěný MIDI Start (0xFA) downstream zařízením; Latch2 analogicky falešný Stop. Stejná třída chyby při bootu: `begin()` nesynchronizuje `lastLatchOn_` s fyzickou polohou přepínačů, takže boot s Latch1 v ON pošle Start na prvním framu. Simulátor (latch jen jako hrany při stisku klávesy) chybu maskuje.
*Oprava:* zapisovat `lastLatchOn_[index] = on` bezpodmínečně před early-returny (potlačit jen akci, ne sledování stavu) a resynchronizovat/absorbovat jeden frame po `enterMode()` a v `begin()`.

**N8 — Arp Latch3 „Reset“ je level-triggered: arp na hardwaru zamrzne dočista, dokud páčka sedí v ON (+ test, který nemůže selhat)**
`core/modes/ArpMode.cpp:86`, test `test/test_arp_mode/test_arp_mode.cpp:235`
Kód `if (in.on) engine_.reset();` nemá edge-detekci (komentář „Reset on rising edge“ lže), zatímco Teensy doručuje úroveň každý frame. S latching přepínačem DFR0789 v klidové poloze ON běží `reset()` tisíckrát za sekundu, `stepTicks_` je trvale nulován rychleji, než přicházejí clock tiky, a arp ztichne, dokud uživatel páčku nepřepne zpět. `BerlinMode` přitom správný edge-detect vzor má (`lastLatch_`). Jediný test Latch3 končí `TEST_ASSERT_TRUE(true)` a doručuje úrovně jednorázově (po vzoru simulátoru), takže kontrakt „úroveň každý frame“ pro Arp nikdo netestuje — pro Berlin ano (`test_held_latch_edge_detect`). MANUAL.md:139 slibuje one-shot reset.
*Oprava:* edge-detekce v `ArpMode::onRawInput` podle vzoru `BerlinMode` + replikovat `test_held_latch_edge_detect` pro ArpMode (držet Latch3 ON přes více framů, ověřit, že arp dál postupuje).

**N9 — `BerlinMode::lastLatch_` zastaralé přes re-entry módu: fantomový Generate zničí Locked sekvenci**
`core/modes/BerlinMode.cpp:66`
`lastLatch_` se v `onEnter()` neresynchronizuje a Latch3 „Generate“ pálí na **jakoukoli** změnu. Scénář: uživatel má Behavior=Locked se sekvencí, kterou chce zachovat, Latch3 je shodou okolností ON; odejde do Settings, tam Latch3 přepne (v Settings funguje jako globální Reset) a vrátí se do Berlin — první frame doručí úroveň lišící se od zastaralé hodnoty, `engine_.generate()` vystřelí a zamčená sekvence je nenávratně pryč bez jediné akce uvnitř Berlin módu. Také první vstup do módu s fyzicky zapnutou páčkou spustí fantomový generate/stop.
*Oprava:* v `onEnter()` resynchronizovat `lastLatch_` na aktuální fyzickou úroveň (např. první frame po vstupu úroveň absorbovat bez akce).

**N10 — `HARDWARE.md` popisuje vyřazený chord-trigger prototyp a cituje neexistující `kPin*` konstanty**
`HARDWARE.md:29`
Čísla pinů sedí, ale sloupec konstant uvádí `kPinMonitorButton`, `kPinEncoderSw/Clk/Dt`, `kPinBpmEncoder*`, `kPinViewEncoder*` — jediný výskyt těchto jmen v repozitáři je HARDWARE.md sám; skutečné konstanty v `platform/teensy/main.cpp:58-93` jsou `kPinLatch1-3`, `kPinEnc1-5*`. Funkční text je z prototypu: pin 2 = „Mapping-mode switch“, View encoder „cykluje Monitor → BigBpm → Notation → Debug“, Enc4/Enc5 „spare/debug-only“, BTN2/BTN3 „wired but unmapped“, „no transport start/stop yet“ — vše v rozporu s realitou (Enc5 = navigace módů, Latch1-3 = transport). CLAUDE.md přitom tento soubor označuje za kanonickou referenci, která musí být v synchronu.
*Oprava:* přepsat sloupce role/konstanty na pojmenování Enc1-5/Latch1-3 a módové runtime role; smazat mapping-mode prózu.

**N11 — `ASSEMBLY.md` walkthrough stále staví a verifikuje chord-mapping prototyp**
`ASSEMBLY.md:311`
Sekce 7g („panel switch for chord-mapping mode“, typy akordů maj/min/dim/…), 7c (rotace cykluje tři display módy), Enc4 „No app-level action“ a závěrečná verifikace („configured chord plays“) popisují firmware, který už neexistuje — main registruje Monitoring/Arp/Berlin/BPM/Settings/Debug přes AppShell. Stavitel postupující podle návodu dnes verifikační kroky nemůže dokončit a diagnostikoval by to mylně jako chybu zapojení. Tabulky zapojení a piny jsou stále správné.
*Oprava:* přepsat sekce 6–7g a verifikační checklist okolo mode-based UI Enc1-5/Latch1-3 (Debug mód je přirozená bring-up verifikace).

**N12 — `CLAUDE.md`: „Current scope“ a hardwarové odstavce popisují starý tří-enkodérový chord trigger; „listened channel knob“ ukazuje na mrtvý kód**
`CLAUDE.md:1` (intro, řádky ~43, ~63, ~163)
Úvod, sekce „Current scope“ („three views + mapping editor“) i odstavec „Three rotary encoders are wired… pin 2 drives mapping mode“ popisují vyřazenou aplikaci, zatímco pozdější sekce o manuálu téhož souboru správně referencuje Enc1–5/Latch1–3 — soubor si vnitřně protiřečí. Závěrečná sekce instruuje měnit `core/MidiMonitorApp.h::kDefaultChannel`, ale `MidiMonitorApp` není zapojen do žádného binárky; skutečný zdroj pravdy je `AppShell::midiInChannel_` nastavovaný v Settings. Jako always-loaded projektové instrukce to aktivně mate každou budoucí session.
*Oprava:* nahradit intro, „Current scope“, odstavce o enkodérech/přepínači a sekci „listened channel“ realitou AppShell/módů (`AppShell::midiInChannel_` + SettingsMode).

## Drobnosti (minor)

| Soubor | Jedna věta | Návrh |
|---|---|---|
| `core/MidiMonitorApp.cpp:1` (+ `ChordEngine.*`, `splash_image.h`, `mapping_prompt_image.h`) | Mrtvá legacy aplikace (~2 500 řádků + 2× 153,6 KB constexpr bitmap) se kompiluje do všech tří buildů; nativní binárka nese ~300 KB nepoužitých dat a flash závisí jen na `--gc-sections`. | Smazat (nebo přesunout do attic/ mimo `build_src_filter`) včetně `scripts/build_splash_image.py`. |
| `core/ClockFollower.cpp:14` | Průměrovací okno nemá detekci mezery — po pauze a obnovení externího clocku se na ~1 dobu zobrazí falešných 30 BPM. | Resetovat follower na externí Start/Stop nebo restartovat okno při gapu > ~100 ms. |
| `platform/teensy/TeensyMidiOutput.cpp:32` (+ host) | `setClockBpm(0)` nenuluje pending tiky — po přepnutí zpět na Internal se přehrají jako fantomový burst. | Vynulovat čítač tiků v `setClockBpm(0)` na obou platformách. |
| `platform/host/RtMidiOutput.cpp:72` | Clock vlákno po stopu vyšle ještě jeden 0xF8 a `join()` může zablokovat UI až o celou periodu (83 ms při 30 BPM). | Re-check `clockRunning_` po sleepu; spát po malých krocích nebo `condition_variable::wait_until`. |
| `platform/host/RtMidiInput.cpp:102` | `queue_` je neomezená — blokovaný SDL loop (drag okna na macOS) nahromadí stovky zpráv, které se pak vyprázdní burstem (BPM spike, arp „sprintuje“). | Cap fronty / koalescence Clock pulzů, nebo timestampovat při enqueue. |
| `platform/host/RtMidiInput.cpp:11` | Destruktor nevolá `cancelCallback()` před `closePort()` — in-flight CoreMIDI callback může běžet proti rozbouranému `mu_`/`queue_` (UB při ukončení). | `midi_->cancelCallback()` před `closePort()`; `midi_` deklarovat poslední. |
| `core/app/AppShell.cpp:157` | Externí Start/Continue/Stop se jen echují ven, neaktualizují `transportState_` ani módy — Latch1 pak pošle redundantní Start místo Pause a u masteru s nepřetržitým clockem enginy projedou skrz Stop. | Routovat externí transport přes `applyTransport()` bez re-echa. |
| `core/ArpEngine.cpp:154` | Při Mute (`muted_`) se potlačí NoteOn, ale NoteOff se posílá vždy — zařízení streamuje orphan NoteOffy pro noty, které nikdy nezačaly. | Flag `soundingSent_` nastavovaný v `emit(true)`; NoteOff na drát jen pokud NoteOn skutečně odešel. |
| `core/app/AppShell.cpp:87` | Overlay early-return nezapíše úroveň do `lastLatchOn_` — na hostu pak Play/Pause vyžaduje dvojstisk, na Teensy se transportní akce po zavření overlaye „dovysílá“ místo potlačení. | Aktualizovat `lastLatchOn_[index]` před early-returnem. |
| `core/modes/DebugMode.cpp:28` | Latch čítače počítají framy, ne přepnutí — na hardwaru se točí rychlostí hlavní smyčky a bring-up telemetrie je bezcenná právě tam, kde má sloužit. | Sledovat poslední úroveň a počítat jen změny stavu. |
| `platform/host/main.cpp:132` | Startovní banner simulátoru uvádí zastaralou sadu módů (chybí Berlin, čísla nesedí od BPM dál). | Aktualizovat na šest módů, ideálně generovat z registrovaných módů. |
| `core/MidiMessage.cpp:51` | `MidiMessage::format()` (~33 řádků, std::string) nemá nikde žádného volajícího — používal jej jen legacy monitor. | Odstranit spolu s legacy aplikací. |
| `core/render/BerlinLayout.h:30` | `berlinIsBlackKey()` reimplementuje fakt, který `KeyLayout.h::isBlackPc()` už exponuje — dvojí místo pro budoucí změny klasifikace kláves. | Volat `isBlackPc(((note % 12) + 12) % 12)`. |
| `core/render/BerlinLayout.h:15` | `drawBerlinParamCell`/dividery duplikují strukturu `ParamGrid.h` — změna stylu param buněk se musí dělat dvakrát. | Extrahovat společné `drawParamCellAt(d, x, y, name, value, valueSize)`. |
| `core/BerlinEngine.cpp:44` | Oba enginy nesou identický čtyřpolíčkový gate/step mikro-scheduler — fix časování gate se musí ladit dvakrát. | Extrahovat sdílený `core/` typ `TickGate`/`StepClock`. |
| `core/Encoder.h:13` | Jména souborů `Encoder.h`/`Button.h` neodpovídají třídám `EncoderInput`/`ButtonInput`; `MidiInput.h` jako jediný živý soubor používá bare include styl. | Přejmenovat hlavičky/třídy do souladu; normalizovat na `#include "core/..."`. |
| `platform/teensy/TeensyMidiInput.cpp:32` | `poll()` na nerozpoznaný typ (Active Sensing 0xFE, SysEx…) vrátí false po zkonzumování zprávy — drain smyčka frame předčasně ukončí a noty/clock za ní čekají na další iteraci. | Skip-and-continue interně; false má znamenat výhradně „fronta prázdná“. |
| `MANUAL.md:226` (+ cs) | Manuál tvrdí, že BPM je při External clocku read-only, ale `BpmMode::onEncoder` nemá žádný guard a tempo edituje. | Buď guard `clockSource()==Internal` + test, nebo změkčit formulaci v obou jazycích. |
| `MANUAL.md:61` (+ cs) | „Při otevřeném overlay se páčky ignorují“ neplatí — capturing módy (Arp/Berlin) latche dostávají a jednají (Latch3 pod overlayem regeneruje sekvenci). | Buď potlačit `fireRaw` pro latche při `overlayOpen_` (+ test), nebo opravit oba manuály. |
| `test/test_clock/test_clock.cpp:33` | Přepnutí zdroje hodin uprostřed znějící noty je netestované, ač komentář v `setClockSource` hazard (odložený gate-off → visící nota) sám přiznává. | Testy Internal↔External mid-note; zmínit limitaci v MANUAL sekci 6. |
| `test/test_berlin_engine/test_berlin_engine.cpp:136` | Morph 1–99 (pravděpodobnostní splice, pravidlo délkového mismatch, `setLength`) nemá žádné pokrytí — a knob Morph se pohybuje po 5, takže právě 5–95 je reálná uživatelská cesta. | Seedovaný test morph=50 s assertem částečného diffu a správné délky napříč generátory; Evolve test na Phasing sekvenci. |

## Vyvrácené nálezy

Žádný kandidátní nález nebyl při verifikaci vyvrácen — všech 17 potvrzených zjištění (po sloučení duplicit 12 číslovaných + drobnosti) obstálo při nezávislé kontrole kódu, u N1, N2, N4 a N5 včetně spustitelné reprodukce, u N2 ověřením vendorovaného Teensy jádra (`usb_midi.c`). Verifikace pouze upravila závažnost u čtyř nálezů (ArpMode Latch3 freeze, externí Stop, usbMIDI race v midi dimenzi, Morph test gap) na základě dosažitelnosti a možnosti zotavení.

## Doporučené pořadí oprav

1. **N1 — jednořádkový NoteOff guard v `BerlinEngine::emitStep`** (kritický stuck-note, triviální fix, doplnit test s `gateTicks > stepLen`).
2. **Latch-kontrakt cluster: N7 + N8 + N9** (pár řádků každý — bezpodmínečný zápis `lastLatchOn_`, edge-detect v ArpMode, resync `lastLatch_` v `BerlinMode::onEnter`; společný test „úroveň každý frame“ pro Arp).
3. **Jednořádkové opravy ArpEngine: N5** (clear `latchHasPending_`) **a N4** (horní bity LCG).
4. **N2 — usbMIDI race**: přesunout vysílání F8 z ISR do hlavní smyčky (malá, ale pečlivá změna; eliminuje OOB zápis i ztracené NoteOffy).
5. **N6 + minor externí transport**: routovat externí Start/Continue/Stop přes `applyTransport()` a flushovat noty na Stop.
6. **N3 — tempo-neutrální swing** (změna chování → upravit existující test, ověřit i no-op pásmo při 1/16).
7. **Smazání legacy aplikace** (`MidiMonitorApp`, `ChordEngine`, image headery, `format()`, `build_splash_image.py`) — jedna úklidová commit, odblokuje refaktory `Display`/`MidiOutput`.
8. **Dokumentace: N12 (CLAUDE.md), N10 (HARDWARE.md), N11 (ASSEMBLY.md)** + obě MANUAL korekce (overlay, BPM read-only) — ideálně po doběhnutí behaviorálních fixů výše, ať se píší jen jednou.
9. **Testovací mezery**: Morph 1–99, Evolve na Phasing, clock-source switching mid-note.
10. **Zbývající minors** (ClockFollower gap, host clock thread, fronty RtMidi, DebugMode čítače, banner, render/naming deduplikace) průběžně.