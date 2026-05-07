# Umsetzung: Software-Fensterdiskriminator für AIOC ohne Hardware-PTT

## 1. Ausgangslage

Der AIOC verfügt in diesem Szenario über kein separates Hardware-PTT-Signal.  
Damit kann die Sendung nicht über eine eindeutige externe TX-Keying-Leitung gehalten werden.

Stattdessen wird ein virtuelles COS-Signal aus dem Audiosignal abgeleitet:

```text
Audio vorhanden  -> COS aktiv
Audio fehlt      -> COS inaktiv
```

Das führt zwangsläufig zu einem VOX-ähnlichen Verhalten.  
Besonders Sprechpausen können dazu führen, dass COS kurzzeitig abfällt.

Ziel dieser Umsetzung ist es, das Verhalten durch einen Software-Fensterdiskriminator zu verbessern.

Wichtig:  
Ein Software-Fensterdiskriminator kann echtes Hardware-PTT nicht vollständig ersetzen.  
Er kann das COS-Flattern aber deutlich reduzieren.

---

## 2. Ziel der Lösung

Die Software soll nicht mehr nur stumpf auf „Audio da / Audio weg“ reagieren.

Stattdessen soll sie prüfen:

- Ist der Audiopegel deutlich über dem Rauschboden?
- Liegt das Signal im erwarteten Sprachbereich?
- Ist das Signal lang genug vorhanden?
- Handelt es sich nur um einen kurzen Knackser?
- Ist die Sprechpause kurz genug, um COS weiter aktiv zu halten?

Das Ergebnis ist ein stabileres virtuelles COS.

---

## 3. Grundprinzip

Die Logik besteht aus folgenden Bausteinen:

```text
Audioeingang
  -> Sprachbandfilter
  -> Pegelmessung / RMS
  -> Rauschbodenbewertung
  -> Fensterdiskriminator
  -> Hysterese
  -> Attack-Time
  -> Hang-Time
  -> virtuelles COS
```

---

## 4. Signalverarbeitung

### 4.1 Audio einlesen

Das Audiosignal wird in kurzen Blöcken verarbeitet.

Empfohlene Blockgröße:

```text
10 ms bis 20 ms
```

Beispiel:

```text
Samplerate:     48000 Hz
Blockgröße:     960 Samples
Blockdauer:     20 ms
```

---

### 4.2 Sprachbandfilter

Für die COS-Erkennung sollte nicht das komplette Audiospektrum bewertet werden.

Empfohlenes Auswertefenster:

```text
300 Hz bis 3000 Hz
```

Dadurch werden reduziert:

- tieffrequentes Brummen
- Netzbrumm
- Körperschall
- sehr hohe Rauschanteile
- kurze HF- oder USB-Störungen

Das ist keine perfekte Spracherkennung, aber klassisch und praxisgerecht.

---

### 4.3 RMS-Pegel berechnen

Für jeden Audioblock wird der RMS-Wert berechnet.

Formel:

```text
RMS = sqrt((x1² + x2² + ... + xn²) / n)
```

Der RMS-Wert ist besser geeignet als ein reiner Spitzenwert, weil kurze Klicks oder Impulse sonst zu leicht COS auslösen würden.

---

## 5. Rauschboden bestimmen

Beim Start der Software sollte für kurze Zeit der Ruhepegel gemessen werden.

Empfehlung:

```text
Messdauer: 2 Sekunden
```

Während dieser Zeit sollte kein Nutzsignal anliegen.

Aus dem gemessenen Rauschboden werden die Schaltschwellen abgeleitet.

Beispiel:

```text
Gemessener Rauschboden: -55 dBFS

COS_ON_THRESHOLD:       -40 dBFS
COS_OFF_THRESHOLD:      -47 dBFS
```

Oder relativ:

```text
COS_ON_THRESHOLD  = noise_floor + 15 dB
COS_OFF_THRESHOLD = noise_floor + 8 dB
```

---

## 6. Hysterese

Es werden zwei unterschiedliche Schaltschwellen verwendet.

```text
Einschaltschwelle:  höher
Ausschaltschwelle: niedriger
```

Beispiel:

```text
COS EIN:  Audiopegel > Rauschboden + 15 dB
COS AUS:  Audiopegel < Rauschboden + 8 dB
```

Dadurch wird verhindert, dass COS an der Schaltschwelle flattert.

Ohne Hysterese wäre das Verhalten zu nervös.

---

## 7. Zeitverhalten

### 7.1 Attack-Time

COS soll nicht sofort beim ersten Audioblock einschalten.

Empfohlener Startwert:

```text
Attack-Time: 80 ms
```

Das bedeutet:

```text
Audio muss mindestens 80 ms gültig sein,
bevor COS aktiv wird.
```

Damit werden kurze Knackser oder Schaltgeräusche ignoriert.

---

### 7.2 Hang-Time

Nach Ende des Audiosignals soll COS nicht sofort abfallen.

Empfohlener Startwert:

```text
Hang-Time: 1000 ms
```

Das bedeutet:

```text
Wenn Audio kurz weg ist,
bleibt COS noch 1000 ms aktiv.
```

Dadurch werden normale Sprechpausen überbrückt.

---

### 7.3 Mindest-Audiozeit

Zusätzlich kann eine Mindestdauer für gültiges Audio verwendet werden.

Empfehlung:

```text
Minimum Valid Audio: 60 ms bis 100 ms
```

Das deckt sich praktisch mit der Attack-Time.

---

## 8. Zustandsmaschine

Die Logik sollte als einfache Zustandsmaschine aufgebaut werden.

```text
+-------+
| IDLE  |
+-------+
    |
    | Audio über ON-Schwelle erkannt
    v
+--------+
| ATTACK |
+--------+
    |
    | Audio bleibt gültig bis Attack-Time erreicht
    v
+------------+
| COS_ACTIVE |
+------------+
    |
    | Audio fällt unter OFF-Schwelle
    v
+------+
| HANG |
+------+
    |
    | Audio kommt zurück
    v
+------------+
| COS_ACTIVE |
+------------+

Wenn im HANG-Zustand kein Audio zurückkommt
und die Hang-Time abläuft:

+------+
| HANG |
+------+
    |
    | Hang-Time abgelaufen
    v
+-------+
| IDLE  |
+-------+
```

---

## 9. Zustände im Detail

### 9.1 IDLE

COS ist inaktiv.

Bedingung für Wechsel nach ATTACK:

```text
audio_level > COS_ON_THRESHOLD
```

---

### 9.2 ATTACK

Es wurde Audio erkannt, aber COS wird noch nicht sofort aktiviert.

Bedingung für Wechsel nach COS_ACTIVE:

```text
audio_level > COS_ON_THRESHOLD
für mindestens ATTACK_TIME
```

Wenn das Signal vorher wieder verschwindet:

```text
zurück nach IDLE
```

---

### 9.3 COS_ACTIVE

COS ist aktiv.

Es bleibt aktiv, solange:

```text
audio_level > COS_OFF_THRESHOLD
```

Wenn der Pegel unter die Ausschaltschwelle fällt:

```text
Wechsel nach HANG
```

---

### 9.4 HANG

COS bleibt noch aktiv, obwohl gerade kein gültiges Audio erkannt wird.

Wenn Audio wiederkommt:

```text
zurück nach COS_ACTIVE
```

Wenn kein Audio wiederkommt und die Hang-Time abläuft:

```text
COS aus
zurück nach IDLE
```

---

## 10. Startparameter

Diese Werte sind als vernünftiger Anfang gedacht:

```text
SAMPLE_RATE              = 48000 Hz
BLOCK_SIZE               = 20 ms
FILTER_LOW               = 300 Hz
FILTER_HIGH              = 3000 Hz

NOISE_MEASURE_TIME       = 2 s

COS_ON_MARGIN            = +15 dB über Rauschboden
COS_OFF_MARGIN           = +8 dB über Rauschboden

ATTACK_TIME              = 80 ms
HANG_TIME                = 1000 ms

MIN_VALID_AUDIO_TIME     = 80 ms
```

---

## 11. Pseudocode

```pseudo
state = IDLE

noise_floor = measure_noise_floor(2 seconds)

cos_on_threshold  = noise_floor + 15 dB
cos_off_threshold = noise_floor + 8 dB

attack_timer = 0
hang_timer = 0

loop forever:

    audio_block = read_audio_block()

    filtered_audio = bandpass_filter(audio_block, 300 Hz, 3000 Hz)

    level = calculate_rms_dbfs(filtered_audio)

    if state == IDLE:

        if level > cos_on_threshold:
            attack_timer = 0
            state = ATTACK

    else if state == ATTACK:

        if level > cos_on_threshold:
            attack_timer += block_duration

            if attack_timer >= ATTACK_TIME:
                set_COS(true)
                state = COS_ACTIVE

        else:
            attack_timer = 0
            state = IDLE

    else if state == COS_ACTIVE:

        if level > cos_off_threshold:
            hang_timer = 0

        else:
            hang_timer = 0
            state = HANG

    else if state == HANG:

        if level > cos_off_threshold:
            hang_timer = 0
            state = COS_ACTIVE

        else:
            hang_timer += block_duration

            if hang_timer >= HANG_TIME:
                set_COS(false)
                state = IDLE
```

---

## 12. Erweiterte Logik gegen Fehltrigger

Optional können zusätzliche Prüfungen eingebaut werden.

### 12.1 Impulsunterdrückung

Kurze starke Peaks werden ignoriert, wenn sie kürzer als z. B. 40 ms sind.

```text
Peak erkannt, aber Dauer < 40 ms -> ignorieren
```

---

### 12.2 Sprachband-Energie

Das Signal wird nur als gültig bewertet, wenn genügend Energie im Sprachbereich liegt.

Beispiel:

```text
300 Hz bis 3000 Hz muss dominieren
```

Das hilft gegen:

- Brummen
- Klicks
- breitbandiges Rauschen
- Störungen außerhalb des Sprachbereichs

---

### 12.3 Maximalpegelprüfung

Ein extrem hoher kurzer Pegel kann als Knackser gewertet werden.

Beispiel:

```text
Peak sehr hoch,
RMS aber sehr kurz,
Dauer unter Mindestzeit
-> kein COS
```

---

## 13. Einstellbare Parameter

Die folgenden Parameter sollten in einer Konfigurationsdatei einstellbar sein:

```ini
[audio]
sample_rate = 48000
block_ms = 20
filter_low_hz = 300
filter_high_hz = 3000

[cos]
noise_measure_seconds = 2
on_margin_db = 15
off_margin_db = 8
attack_ms = 80
hang_ms = 1000
min_valid_audio_ms = 80
```

---

## 14. Testplan

### 14.1 Ruhezustand

Prüfen:

```text
Kein Audio -> COS bleibt aus
```

---

### 14.2 Normale Sprache

Prüfen:

```text
Sprechen -> COS geht an
normale kurze Pausen -> COS bleibt an
Ende der Sprache -> COS fällt nach Hang-Time ab
```

---

### 14.3 Leise Sprache

Prüfen:

```text
Leise Sprache darf COS nicht verlieren
```

Falls COS abfällt:

```text
ON-Margin reduzieren
OFF-Margin reduzieren
Hang-Time erhöhen
```

---

### 14.4 Hintergrundgeräusche

Prüfen:

```text
Lüfter, Brummen, Raumgeräusche -> COS bleibt aus
```

Falls COS fälschlich auslöst:

```text
ON-Margin erhöhen
Sprachbandfilter enger setzen
Attack-Time erhöhen
```

---

### 14.5 Sprechpausen

Prüfen:

```text
kurze Pause 300 ms -> COS bleibt an
Pause 800 ms       -> COS bleibt idealerweise an
Pause > 1500 ms    -> COS darf abfallen
```

---

## 15. Bewertung der Lösung

### Vorteile

- deutlich stabileres COS
- weniger VOX-Flattern
- kurze Sprechpausen werden überbrückt
- einfache Umsetzung
- keine zusätzliche Hardware nötig
- Parameter können an reale Pegel angepasst werden

### Nachteile

- kein echter Ersatz für Hardware-PTT
- lange Pausen bleiben problematisch
- zu lange Hang-Time hält TX unnötig offen
- zu niedrige Schwellen können Rauschen auswerten
- zu hohe Schwellen schneiden leise Sprache ab

---

## 16. Klare technische Grenze

Die Software kann nur bewerten, ob Audio vorhanden ist.

Sie kann nicht sicher wissen, ob der Benutzer weiter senden möchte, wenn gerade keine Sprache vorhanden ist.

Deshalb gilt:

```text
Software-COS kann Hardware-PTT nachahmen,
aber nicht vollständig ersetzen.
```

Das ist keine Schwäche der konkreten Umsetzung, sondern eine prinzipielle Grenze.

---

## 17. Empfohlene Grundeinstellung

Für den ersten Praxistest:

```text
Attack-Time:       80 ms
Hang-Time:         1000 ms
ON-Schwelle:       Rauschboden + 15 dB
OFF-Schwelle:      Rauschboden + 8 dB
Sprachfilter:      300 Hz bis 3000 Hz
Blockgröße:        20 ms
Rauschmessung:     2 Sekunden
```

Wenn COS bei Sprechpausen noch fällt:

```text
Hang-Time auf 1200 ms bis 1500 ms erhöhen
```

Wenn COS zu lange hängen bleibt:

```text
Hang-Time auf 500 ms bis 800 ms verringern
```

Wenn leise Sprache nicht erkannt wird:

```text
ON-Margin auf 12 dB reduzieren
OFF-Margin auf 6 dB reduzieren
```

Wenn Rauschen COS auslöst:

```text
ON-Margin auf 18 dB erhöhen
Attack-Time auf 100 ms erhöhen
```

---

## 18. Kurzfazit

Die sinnvolle Umsetzung ist kein einfacher VOX-Schalter, sondern ein träger, hysterese-behafteter Software-COS.

Empfohlene Struktur:

```text
Rauschbodenmessung
+ Sprachbandfilter
+ RMS-Pegelbewertung
+ Hysterese
+ Attack-Time
+ Hang-Time
+ Zustandsmaschine
```

Damit wird das Verhalten deutlich näher an eine klassische COS/PTT-Logik gebracht.

Vollständig eliminieren lässt sich das Problem ohne echtes PTT aber nicht.
