# MIDIops — Uživatelský manuál

*Jazyky: [English](MANUAL.md) · **Čeština***

MIDIops je hardwarový MIDI nástroj postavený na Teensy 4.1 s 2,8" (320×240)
displejem. Běží v něm několik **módů** — MIDI monitor, **arpeggiátor**
respektující stupnici, generativní **Berlin-School sekvencer**, zobrazení
velkého tempa, **nastavení** a **debug**. Stejný software běží i v macOS
simulátoru (`make sim`), takže vše v tomto manuálu platí pro reálné zařízení
i simulátor.

Manuál vysvětluje každé ovládání a každý parametr. Pro zapojení a postup stavby
viz [`HARDWARE.md`](HARDWARE.md) a [`ASSEMBLY.md`](ASSEMBLY.md).

---

## 1. Ovládání v kostce

Na předním panelu je **pět rotačních enkodérů** (Enc1–Enc5, každý je i tlačítko)
a **tři páčkové (latching) spínače** (Latch1–Latch3).

| Ovládání | Funkce |
|---|---|
| **Enc1–Enc4** (otáčení) | Editují čtyři parametry aktuální obrazovky (jeden knob na buňku, zleva doprava). |
| **Enc1–Enc4** (stisk) | Rezervováno (v běžných módech zatím bez funkce). |
| **Enc5** (otáčení) | **Přepínání obrazovek** v rámci módu. |
| **Enc5** (stisk) | Otevře **překryv pro výběr módu** (viz §3). |
| **Latch1–Latch3** | **Transportní tlačítka** — každé stisknutí je jedno cvaknutí; software přepíná/jedná podle svého aktuálního stavu (fyzická poloha páčky a její LED nemají žádný význam). Význam závisí na aktivním módu (viz jednotlivé módy). |

Klávesy simulátoru: Enc1 = `1`/`2`/`3` (vlevo/stisk/vpravo), Enc2 = `4`/`5`/`6`,
Enc3 = `7`/`8`/`9`, Enc4 = `0`/`-`/`=`, Enc5 = `Q`/`W`/`E`. Latch1 = `Space`,
Latch2 = `Backspace`, Latch3 = `Return` (každé stisknutí klávesy = jedno cvaknutí tlačítka). Noty
zahraješ klávesami `z x c v b n m` (bílé klávesy C4–B4); `Shift`+`1…9` volí
kanál, na který se noty pošlou; `Esc` ukončí.

---

## 2. Displej

Vždy je nahoře **10pixelová horní lišta**:

```
Berlin  -  structure (1/4)        ♩120  ▶
```

Ukazuje **název módu**, **aktuální obrazovku** a její pořadí
(`1/4` = obrazovka 1 ze 4), **tempo (BPM)** a **stav transportu**.

Většina parametrových obrazovek zobrazuje mřížku **buněk**, každá je jeden
parametr: malý **název** nad velkou **hodnotou**. Vizualizační obrazovky (worms,
notace, piano-roll) vyplňují plochu pod horní lištou.

---

## 3. Pohyb mezi módy a obrazovkami

- **Přepnout obrazovku** (v rámci módu): otoč **Enc5**. Obrazovky se cyklí
  dokola.
- **Přepnout mód**: stiskni **Enc5** pro otevření **překryvu výběru módu** —
  názvy módů leží v jednom vodorovném řádku, každý s ikonou nad názvem
  (osciloskop = Monitoring, stoupající noty = Arp, sloupce sekvenceru = Berlin,
  metronom = BPM, posuvníky = Settings, brouk = Debug), a výběr označuje
  rámeček uprostřed obrazovky. Položka v rámečku je vykreslená největší;
  se vzdáleností od středu se položky zmenšují a tmavnou. Otáčením **Enc5** se řádek posouvá
  doleva/doprava — klouže plynule, jako převíjení pásku, a cyklí dokola.
  Dalším stiskem **Enc5** vstoupíš do módu v rámečku. Překryv
  se po pár sekundách bez vstupu sám zavře. Dokud je překryv otevřený, páčky
  neovládají globální transport, ale vlastní funkce páček aktivního módu
  (např. Berlin Generate) stále platí.

Zařízení startuje v **Berlinu**. Pořadí módů: **Monitoring · Arp · Berlin ·
BPM · Settings · Debug**.

---

## 4. Globální pojmy

Několik nastavení je **globálních** — sdílí je všechny módy (hlavně Arp a
Berlin). Měníš je v **Settings** a **BPM**:

- **Stupnice a Root** (Settings → Scale): hudební stupnice a tónální centrum.
  Každá generovaná/arpeggiovaná nota se do této stupnice kvantizuje. Změna
  ovlivní Arp i Berlin.
- **Tempo (BPM)** (mód BPM): rychlost clocku pro všechny clock-řízené módy.
  Rozsah **30–300**, výchozí **120**.
- **Zdroj clocku** (Settings → MIDI): **Internal** (clock generuje zařízení)
  nebo **External** (zařízení následuje příchozí MIDI clock 24 PPQN a zobrazuje
  následované tempo).
- **MIDI Out kanál** (Settings → MIDI): kanál, na který se posílají noty
  **Arpu**. Rozsah **1–16**, výchozí **1**. (Berlin ho nepoužívá — každý z jeho
  čtyř hlasů má vlastní kanál na obrazovce `voices` v Berlinu.)
- **MIDI In kanál** (Settings → MIDI): z jakého kanálu se přijímají příchozí
  noty. **OMNI (0)** přijímá všechny kanály; **1–16** filtruje na jeden.
- **Transport** (Settings → MIDI): **Send** (výchozí) = zařízení vysílá MIDI
  Start/Continue/Stop, aby ho DAW mohl sledovat; **Recv** = zařízení následuje
  příchozí MIDI transport (Start = hraje od začátku, Continue = pokračuje,
  Stop = zastaví a utiší); **Off** = ani neposílá, ani nenásleduje. Nastavení
  je nezávislé na nastavení zdroje clocku.

Všechna výše uvedená nastavení — včetně tempa — se **ukládají automaticky**
zhruba 2 sekundy po poslední změně a po dalším zapnutí se obnoví.
**Settings → System** nabízí **factory reset**, který je všechna vrátí na
výchozí hodnoty uvedené zde.

Ve vizualizacích je **kanál 1 zelený**; ostatní kanály mají vlastní barvy, aby
byl vstup a výstup hned čitelný.

---

## 5. Módy

### 5.1 Monitoring

Sledování příchozích MIDI not v reálném čase. Bez parametrů — jen dvě zobrazení:

| Obrazovka | Co ukazuje |
|---|---|
| **worms** | Barevné „žížaly" podle kanálu, stoupající z klaviatury, dokud jsou noty drženy. |
| **notes** | Notový zápis posledních příchozích not. |

Páčky tu fungují jako **globální transport** (Play/Pause · Stop · Reset), který
posílá MIDI Start/Stop/Continue, je-li **Transport** nastaven na **Send**.

### 5.2 Arp — arpeggiátor

Arpeggiátor respektující stupnici. Drž nebo pošli noty; engine je přehraje jako
arpeggio na **MIDI Out kanál**, zamčené na clock. Noty se řadí do **FIFO**
fronty, takže se nikdy nepřekrývají.

**Obrazovky:** `params1` · `params2` · `worms` · `notes` · `presets`.

**Obrazovka `params1`:**

| Knob | Parametr | Rozsah | Výchozí | Význam |
|---|---|---|---|---|
| Enc1 | **Steps** | 1–16 | 3 | Počet kroků v jednom cyklu arpeggia. |
| Enc2 | **Rate** | 1/4, 1/8, 1/8T, 1/16, 1/16T, 1/32 | 1/16 | Délka kroku v notových hodnotách. |
| Enc3 | **Gate** | 10–100 % | 80 | Jak dlouho nota zní v rámci kroku (krátké = staccato). |
| Enc4 | **Direction** | Up, Down, UpDown, DownUp, Random | Up | Pořadí, ve kterém se tóny akordu hrají. |

**Obrazovka `params2`:**

| Knob | Parametr | Rozsah | Výchozí | Význam |
|---|---|---|---|---|
| Enc1 | **Octave** | −2…+2 | 0 | Transpozice arpeggia v oktávách. |
| Enc2 | **Swing** | 50–75 % | 50 | Zpoždění každého druhého kroku pro swing (50 = rovně). |
| Enc3 | **Velocity** | Fixed, Follow, Accent | Fixed | Pevná úroveň, podle vstupu, nebo s akcenty. |
| Enc4 | *(stav)* | — | — | Ukazuje stav **Hold** a **Mute** (jen pro čtení). |

Obrazovky `worms` a `notes` vizualizují **odchozí** arpeggio.

**Obrazovka `presets`:** ukládání, načítání a mazání parametrů Arpu ve **20
slotech**. Stiskem **Enc1 = Save**, **Enc2 = Load** nebo **Enc3 = Delete**
otevřeš výběr slotu — mřížku slotů 01–20, kde obsazené sloty svítí, prázdné
jsou tmavé a výběr má rámeček. Otáčením kteréhokoli z Enc1–4 vybereš slot a
**dalším stiskem téhož enkodéru potvrdíš**; stisk jiného enkodéru ruší,
stejně jako 5 s bez vstupu nebo odchod z obrazovky. Číslo slotu se pamatuje,
takže save → load zůstane na stejném slotu. Save obsazený slot rovnou
přepíše; Delete nechá výběr otevřený, takže můžeš vyčistit víc slotů za
sebou; Load/Delete na prázdném slotu jen bliknou `EMPTY`.

**Transport (páčky):**

| Latch | Funkce |
|---|---|
| **Latch1 — Hold** | Stisk přepíná **Hold** (stav je zobrazen na obrazovce params2): zapnuto = drží/opakuje aktuální notu donekonečna; vypnuto = každou notu z fronty zahraje jednou a posune se dál. |
| **Latch2 — Mute** | Stisk přepíná **Mute** (stav je zobrazen na obrazovce params2): zapnuto = přestane posílat noty (sekvence běží potichu dál); vypnuto = zase zní. |
| **Latch3 — Reset** | Stisk restartuje arpeggio od prvního kroku. |

### 5.3 Berlin — generativní sekvencer

**Čtyřhlasý** generativní sekvencer ve stylu Berlin School. Každý hlas si
**vygeneruje** vlastní krátkou smyčkovou sekvenci a přehrává ji na clock; čtyři
hlasy dohromady staví klasickou vrstvenou berlínskou texturu:

- **Bass** — kořenový „heartbeat". Staví ho vlastní root-anchor generátor (root
  na silných dobách, občas kvinta/oktáva, krátké gaty), takže se na něj knob
  **Algorithm** nevztahuje. Výchozí: oktáva C1, length 16, density 30 %, gate
  50 %, kanál 1.
- **Mid** — pluck figura. Výchozí: oktáva C3, length **15**, kanál 2.
- **High** — pohyblivá melodie a hlas, který je editovaný jako výchozí.
  Výchozí: oktáva C4, length 16, kanál 3.
- **Lead** — řídká, vysoká melodie, která hraje **call-and-response** s High:
  hraje v mezerách High (při **Generate** se každý krok Leadu, který koliduje
  s aktivním krokem High, deaktivuje). Výchozí: oktáva C4 s range 2 (takže
  pokrývá C4–C6), length 16, density 30 % (řídké, hodně pauz), gate 85 %
  (legato), kanál 4.

**15** kroků Midu proti **16** ostatních hlasů je signature **note-phasing**
žánru: hlasy sdílí tempo, ale jejich smyčky mají různé délky, takže se v průběhu
mnoha taktů rozcházejí a zase scházejí.

Přehrávání řídíš páčkami a hudbu tvaruješ pěti parametrovými obrazovkami. Dole
na každé obrazovce je **piano-roll** všech čtyř hlasů naráz (viz níže).

**Obrazovky:** `structure` · `character` · `voices` · `dynamics` · `behavior` ·
`presets` (piano-roll zůstává na parametrových obrazovkách — mění se jen horní
řádek parametrů).

**Po hlasech vs. globální.** `structure` a `character` editují **jeden hlas
naráz** (*editovaný hlas*); `dynamics` a `behavior` jsou **globální** a platí
pro všechny čtyři hlasy společně. Obrazovka `voices` je mixer (jedna buňka na
hlas). Na obou per-voice obrazovkách **stisk Enc1 / Enc2 / Enc3 / Enc4 vybere
přímo hlas** (Bass / Mid / High / Lead); **mute je jen na mixeru `voices`**, ne
tady. Jméno editovaného hlasu je vpravo nahoře nad piano-rollem v jeho barvě.
Každá per-voice buňka zobrazuje **hodnoty všech čtyř hlasů pod sebou** (nahoře
Lead, pak High, pak Mid, dole Bass): hodnota vybraného hlasu je zvýrazněná bíle,
zatímco ostatní jsou tmavší, takže máš všechny hlasy na očích naráz a ten
aktivní vyniká.

**Obrazovka `structure`** (po hlasech):

| Knob | Parametr | Rozsah | Výchozí | Význam |
|---|---|---|---|---|
| Enc1 | **Algorithm** | Walk, Phase, Degree | Walk | Metoda generování (viz níže). Pro **Bass** buňka ukazuje „Bass" a je zamčená — Bass vždy používá vlastní root-anchor generátor. |
| Enc2 | **Length** | 3–32 | 16 (Mid 15) | Počet kroků smyčky tohoto hlasu. |
| Enc3 | **Density** | 0–100 % | 50 (Bass 30, Lead 30) | Kolik kroků hraje notu vs. pauzu. |
| Enc4 | **AlgoPrm** | — | — | Jedna sdílená buňka: **Scatter** (1–7) pod Walk, **GateLen** (3–16) pod Phase. Pod Degree a pro Bass se zobrazí zašedlé „-". |

**Obrazovka `character`** (po hlasech):

| Knob | Parametr | Rozsah | Výchozí | Význam |
|---|---|---|---|---|
| Enc1 | **Gate** | 40–99 % | 55 (Bass 50, Lead 85) | Délka noty v rámci kroku. Funguje živě při přehrávání. |
| Enc2 | **Tension** | 0–100 % | 30 | Nízká = tóny se drží root/kvinty (bezpečné); vysoká = odvážnější. |
| Enc3 | **Octave base** | C1–C5 | Lead C4, High C4, Mid C3, Bass C1 | Nejnižší oktáva hlasu. |
| Enc4 | **Octave range** | 1–3 | 1 (Lead 2) | Přes kolik oktáv se noty rozprostřou. V Live rozšíření/zúžení melodii proporcionálně roztáhne/stáhne (ve stupnici; root kotva zůstává). |

**Obrazovka `voices`** (mixer — jedna buňka na hlas):

| Knob | Parametr | Rozsah | Výchozí | Význam |
|---|---|---|---|---|
| Enc1 | **Bass** kanál / mute | 1–16 | 1 | Otáčením nastavíš MIDI kanál Bassu; **stiskem mute/unmute**. |
| Enc2 | **Mid** kanál / mute | 1–16 | 2 | Otáčením nastavíš MIDI kanál Midu; stiskem mute/unmute. |
| Enc3 | **High** kanál / mute | 1–16 | 3 | Otáčením nastavíš MIDI kanál Highu; stiskem mute/unmute. |
| Enc4 | **Lead** kanál / mute | 1–16 | 4 | Otáčením nastavíš MIDI kanál Leadu; stiskem mute/unmute. |

> **Ztlumený** hlas běží potichu dál (jeho sekvence i playhead pokračují), takže
> po unmute naskočí zpátky **ve fázi** s ostatními — tah „build up, then take
> away". Buňka ztlumeného hlasu ukazuje **MUTED** a jeho řádek v rollu je
> vykreslený nejtmavěji.

**Obrazovka `dynamics`** (globální — platí pro všechny hlasy):

| Knob | Parametr | Rozsah | Výchozí | Význam |
|---|---|---|---|---|
| Enc1 | **Velocity** | 1–126 | 100 | Základní velocity not. Funguje živě při přehrávání. |
| Enc2 | **Humanize** | 0–30 | 20 | Náhodné ± kolísání velocity na notu. Funguje živě při přehrávání. |
| Enc3 | **Accent** | 0–27 | 20 | Přídavek velocity na akcentovaných notách (1. doba, root noty). Funguje živě při přehrávání. |
| Enc4 | **Resolution** | 8th, 16th | 8th | Mřížka kroků (8th = klidnější, 16th = hustší). |

**Obrazovka `behavior`** (globální):

| Knob | Parametr | Rozsah | Výchozí | Význam |
|---|---|---|---|---|
| Enc1 | **Behavior** | Lock, Evolve, Live | Live | Jak se sekvence mění v čase (viz níže). |
| Enc2 | **Morph** | 0–100 % | 100 | Jak moc se regenerace liší od aktuální sekvence: 0 % ≈ stejná, 100 % = úplně nová. |
| Enc3 | **Evolve rate** | 1–8 | 4 | (jen Evolve) počet smyček mezi automatickými variacemi. Pod Lock/Live zašedlé a zamčené. |
| Enc4 | — | — | — | Nepoužito. |

> Šedě vykreslené buňky jsou parametry, které aktuální algoritmus/chování
> ignoruje — jejich knoby jsou zamčené, dokud nepřepneš na konfiguraci, která
> je používá.

**Piano-roll** ukazuje **všechny čtyři hlasy naráz** přes sdílenou klaviaturu:
**Bass modře, Mid zeleně, High oranžově, Lead purpurově (magenta)**. **Editovaný hlas** je vykreslený plně
syté, ostatní ztlumeně a ztlumený hlas nejtmavěji. Každý hlas má **vlastní
playhead** — protože mají různé délky, jejich playheady se rozcházejí, takže je
phasing vidět přímo v rollu. **Jas** bloku každé noty stále odpovídá její
velocity (hlasitější = jasnější). Akcenty se už nekreslí bíle — barevná identita
hlasu vždy vyhraje.

**Obrazovka `presets`:** funguje úplně stejně jako obrazovka presets v Arpu
(Enc1 = Save, Enc2 = Load, Enc3 = Delete přes 20 slotů — viz §5.2), s jedním
berlinským bonusem: slot ukládá **celý čtyřhlasý stack** — všechny parametry,
všechny čtyři realizované sekvence a u každého hlasu jeho kanál i stav mute —
takže load vrátí přesně ty patterny, které jsi uložil, žádné nové losování. Load
během přehrávání vymění stack **plynule**: playhead každého hlasu běží dál
(zalomený do nové délky) — ideální pro živé přechody. **Sloty uložené před
přidáním hlasu Lead se zobrazí jako prázdné** a lze je prostě přepsat.

**Algoritmy (Enc1 na `structure`):**

- **Walk** (Drunkard's Walk) — bloudivá melodie: každá nota udělá malý náhodný
  krok (až **Scatter**) od předchozí, vždy ve stupnici; **Tension** váží výběr
  k rootu a kvintě (nízká) nebo od nich (vysoká).
- **Phase** (Gate/Pitch Phasing) — seznam výšek (délka = **Length**) a seznam
  gate (délka = **GateLen**) různých délek běží proti sobě a tvoří dlouhý,
  pomalu se vyvíjející vzor, který „zní náhodně, ale není".
- **Degree** (Degree-Weighted) — každá nota se volí samostatně, vážená ke
  konsonantním stupňům (root/kvinta); **Tension** tuto preferenci zplošťuje.
- Hlas **Bass** tyto tři ignoruje a používá vlastní **root-anchor generátor**:
  root na silných dobách, sem tam kvinta nebo oktáva, krátké gaty — heartbeat
  pod ostatními hlasy.

**Chování (Enc1 na `behavior`):**

- **Lock** — sekvence se točí beze změny; tvoje úpravy parametrů se projeví až
  při dalším **Generate** (Latch3).
- **Evolve** — za hraní se sekvence pomalu mění: každých **Evolve rate** smyček
  se změní 1–2 kroky. Generate stále vytvoří úplně nový vzor.
- **Live** — tvoje úpravy **tvarují stávající sekvenci na místě** při otáčení
  knobu, bez jejího přegenerování a **bez resetu pozice přehrávání** — přehrávání
  běží dál skrz změnu. Live tvarování míří jen na **editovaný hlas**. **Density**
  přidává nebo ubírá noty na nové množství
  (kotva rootu na kroku 1 vždy zůstává); **Octave base/range** transponují a
  složí stávající noty do nového registru (obrys melodie zůstává zachován);
  **Length** zkrátí (pozice přehrávání se zaroluje) nebo prodlouží (vyplní se jen
  nový ocas); **Tension** přeladí noty se zachováním stávajícího rytmu, gate a
  velocity. **Gate**, **Resolution** a **Velocity/Humanize/Accent** fungují živě
  v každém chování (Gate tvaruje hrající noty, Resolution mění krokovou mřížku,
  knoby Dynamics okamžitě přerazítkují velocity). **Algorithm, Scatter a
  GateLen** — které určují, *jak se sekvence vytváří* — se projeví až při dalším
  **Generate** (Latch3), který stále provede plnou regeneraci řízenou parametrem
  **Morph**. **Morph** a **Evolve rate** jsou meta-nastavení, jež řídí tuto
  regeneraci a posun při Evolve.

**Transport (páčky):**

| Latch | Funkce |
|---|---|
| **Latch1 — Play/Pause** | Stisk přepíná hraje/pauza pro **všechny čtyři hlasy** najednou. Pauza drží playheady na místě. (Při Transport = Recv řídí přehrávání DAW; stisk je i tak ruční přepínací zásah.) |
| **Latch2 — Stop** | Stisk přetočí všechny hlasy na krok 1 a utiší je. |
| **Latch3 — Generate** | Stisk přegeneruje **všechny čtyři hlasy** (pak namaskuje Lead proti High kvůli call-and-response), pak provede **vertikální kontrolu konsonance** — kolidující současné noty (malá sekunda nebo tritón) se posunou na konsonantní tón ve stupnici. (Kontrola se přeskočí, je-li **Tension** kteréhokoli hlasu nad 60.) |

> Je-li **Transport = Send**, Latch1 a Latch2 také vysílají MIDI Start/Continue/Stop,
> aby připojený DAW následoval přehrávání zařízení: stisk play posílá Start (nebo
> Continue při obnovení po pauze); stisk Stop posílá Stop.

**MIDI transpozice.** Když jsi v Berlinu, noty přicházející na globálním **MIDI In
kanálu** (Settings → MIDI) transponují celý čtyřhlasý stack **diatonicky** — vše
zůstane v aktuální stupnici. Transpozice je **zamčená**: poslední nota nastaví
nové tonální centrum a to drží až do další noty. Zahráním **rootu stupnice kolem
středního C** se vrátíš domů; vyšší/nižší nota posune melodii nahoru/dolů po
stupních stupnice (včetně celých oktáv). Příchozí nota je tichý ovládací vstup —
nezní. Piano-roll se posune spolu s transpozicí.

### 5.4 BPM

Velké zobrazení tempa. **Enc1** nastavuje globální **BPM** (30–300, výchozí 120).
Když je zdroj clocku **External**, tempo je jen pro čtení a následuje příchozí
clock.

### 5.5 Settings

Globální nastavení, na třech obrazovkách. Každá zdejší změna (a BPM) se uloží
automaticky ~2 s po poslední úpravě a přežije vypnutí.

**Obrazovka `midi`:**

| Knob | Parametr | Rozsah | Výchozí | Význam |
|---|---|---|---|---|
| Enc1 | **MIDI Out kanál** | 1–16 | 1 | Kanál, na který se posílají noty **Arpu**. (Berlin používá vlastní kanály po hlasech — viz §5.3.) |
| Enc2 | **MIDI In kanál** | OMNI, 1–16 | OMNI | Přijímat noty ze všech kanálů (OMNI) nebo jen z jednoho. |
| Enc3 | **Clock** | Internal, External | Internal | Generovat clock, nebo následovat externí. |
| Enc4 | **Transport** | Off, Send, Recv | Send | Send = vysílat Start/Continue/Stop (zařízení je transport master); Recv = následovat příchozí transport; Off = ani jedno. |

**Obrazovka `scale`:**

| Knob | Parametr | Rozsah | Výchozí | Význam |
|---|---|---|---|---|
| Enc1 | **Scale** | Major, Minor, Aug, Dim, Pent+, Pent− | Major | Stupnice, do které se kvantizují všechny noty. |
| Enc2 | **Root** | C … B | C | Tónální centrum. |

**Obrazovka `system`:**

Buňka **FACTORY RESET** ovládaná **stiskem Enc1** (dvoukrokově, aby nešla
spustit omylem): první stisk ji odjistí (`SURE?`), druhý stisk do 3 sekund
vrátí všechna globální nastavení i BPM na výchozí hodnoty a smaže uložené
hodnoty (`DONE`). Pokud nepotvrdíš včas, vrátí se do klidového stavu.
Otáčení knobů na této obrazovce nic nedělá.

### 5.6 Debug

Diagnostická obrazovka ukazující živou aktivitu každého enkodéru a páčky (počty
otoček, poslední delta, počty stisků, stav páček). Užitečné pro ověření
hardwaru. Nedávno změněné řádky se zvýrazní, takže vidíš, který ovladač se hnul.
Bez MIDI výstupu.

---

## 6. Clock a Transport

**Zdroj clocku** (Settings → MIDI, Enc3) a **Transport** (Settings → MIDI, Enc4) jsou nezávislá nastavení.

- **Internal** (výchozí): zařízení je clock master. Posílá MIDI Clock (24 PPQN)
  a Arp/Berlin z něj běží. Tempo nastav v módu **BPM**.
- **External**: zařízení následuje příchozí MIDI Clock. Arp/Berlin postupují s
  každým příchozím pulzem, zobrazené BPM následuje externí tempo a zařízení
  **negeneruje** vlastní clock. Přepnutím zpět na Internal se obnoví.

**Transport** řídí, zda se MIDI Start/Continue/Stop posílají nebo následují, nezávisle na zdroji clocku:

- **Send** (výchozí): zařízení vysílá MIDI transportní zprávy. Globální páčky
  (v nezachycujících módech) a Latch1/Latch2 v Berlinu posílají Start, Continue
  nebo Stop, aby připojený DAW sledoval přehrávání zařízení.
- **Recv**: zařízení následuje příchozí MIDI transport. Start hraje od začátku;
  Continue obnoví z uložené pozice; Stop zastaví a okamžitě utiší hranou notu.
  Zprávy jsou zpracovány a nejsou dál přeposílány.
- **Off**: zařízení ani neposílá, ani nenásleduje transportní zprávy.

**Bezpečnost (vždy aktivní):** při **externím** zdroji clocku příchozí MIDI Stop
vždy okamžitě utiší hranou notu — bez ohledu na nastavení Transportu — protože
DAW zastavující clock by notu nikdy nezavřel přes naplánovaný gate-off.

---

## 7. Rychlý přehled kláves simulátoru

| Klávesy | Ovládání |
|---|---|
| `1` `2` `3` | Enc1 — vlevo / stisk / vpravo |
| `4` `5` `6` | Enc2 — vlevo / stisk / vpravo |
| `7` `8` `9` | Enc3 — vlevo / stisk / vpravo |
| `0` `-` `=` | Enc4 — vlevo / stisk / vpravo |
| `Q` `W` `E` | Enc5 — vlevo / stisk / vpravo (přepínání obrazovek / překryv módů) |
| `Space` | Latch1 (jedno cvaknutí tlačítka) |
| `Backspace` | Latch2 (jedno cvaknutí tlačítka) |
| `Return` | Latch3 (jedno cvaknutí tlačítka) |
| `z x c v b n m` | Zahrát noty C4–B4 |
| `Shift`+`1…9` | Nastavit kanál, na který se zahrané noty pošlou |
| `Esc` | Ukončit |

---

*Tento manuál se udržuje v synchronu s firmwarem. Pokud něco zde nesouhlasí se
zařízením, zdrojem pravdy je kód v `core/` a `platform/teensy/main.cpp` — prosím
nahlas nebo oprav nesrovnalost.*
