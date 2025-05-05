
# Demon synchronizujący katalogi
Projekt implementuje daemon systemowy monitorujący i synchronizujący zawartość dwóch katalogów w systemie operacyjnym. Demon działa w tle, okresowo porównując katalog źródłowy z docelowym i aktualizując zawartość katalogu docelowego na podstawie zmian w katalogu źródłowym.

## 🔍 Główne Funkcje

- **Automatyczna synchronizacja katalogów** - kopiowanie nowych i zmodyfikowanych plików, usuwanie nieobecnych
- **Tryb działania w tle** - działa jako daemon systemowy nie wymagający interakcji z użytkownikiem
- **Obsługa dużych plików** - zoptymalizowane kopiowanie z wykorzystaniem mapowania w pamięci
- **Rekurencyjna synchronizacja** - opcjonalna synchronizacja podkatalogów i ich zawartości
- **Reagowanie na sygnały** - natychmiastowa synchronizacja na żądanie (SIGUSR1)
- **Szczegółowe logowanie** - informacje o wszystkich operacjach w dzienniku systemowym

## 🔧 Kompilacja

```
gcc -o dirsyncd zadanie1.c
```

## 🚀 Użycie
Podstawowe uruchomienie:
```
./dirsyncd <katalog_źródłowy> <katalog_docelowy>
```
Z opcjami dodatkowymi:
```
./dirsyncd [-R] [-s interwał] [-t próg] <katalog_źródłowy> <katalog_docelowy>
```
Paramtery: 

| Opcja | Opis |
|-------|------|
| `-R` | Włącza rekurencyjną synchronizację katalogów |
| `-s interwał` | Ustawia czas między synchronizacjami w sekundach (domyślnie 300s) |
| `-t próg` | Określa próg rozmiaru pliku w bajtach dla metody mmap |

## ⚙️ Zarządzanie demonem

### Sygnały sterujące:

| Akcja | Komenda | Opis |
|-------|---------|------|
| Wymuszenie synchronizacji | `killall -USR1 dirsyncd` | Natychmiastowe rozpoczęcie synchronizacji |
| Zakończenie pracy | `killall -TERM dirsyncd` | Bezpieczne zatrzymanie demona |

### Monitorowanie:

```bash
# Podgląd logów w czasie rzeczywistym
tail -f /var/log/syslog | grep dirsyncd

# Wyświetlenie ostatnich wpisów
grep dirsyncd /var/log/syslog | tail -n 20
```

