#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
FDR Converter — Conversor y Visualizador de Datos de Vuelo
CEV-FAA / IUA

Convierte archivos FDR_XXXX.bin (formato CEV binario) a CSV
y genera gráficos interactivos para análisis post-vuelo.

Uso offline: no requiere internet. Funciona como .exe standalone.
"""

import struct
import sys
import os
import csv
import json
import binascii
import threading
import ctypes
import string
from pathlib import Path
import tkinter as tk
from tkinter import filedialog, messagebox, ttk

import numpy as np
import matplotlib
import matplotlib.pyplot
matplotlib.use("TkAgg")
from matplotlib.backends.backend_tkagg import FigureCanvasTkAgg, NavigationToolbar2Tk
from matplotlib.figure import Figure

# ==============================================================================
# Constantes del formato de archivo FDR
# ==============================================================================
FDR_FILE_MAGIC          = 0xFD464452
FDR_FILE_HEADER_VERSION = 1
FDR_FILE_HEADER_SIZE    = 12
HEADER_STRUCT = struct.Struct("<IBBHI")

# ==============================================================================
# Tablas de sub-conmutación — mirror exacto de sample_format.c
# ==============================================================================

# Word 17 sub-conmutada (Formato 1)
FMT1_WORD17 = [
    "DIG",    "ANA_17", "ANA_18", "ANA_19", "ANA_20", "ANA_21", "ANA_22", "ANA_23",
    "ANA_24", "ANA_25", "DIG",    "ANA_26", "ANA_27", "ANA_28", "ANA_29", "ANA_30",
    "ANA_31", "ANA_32", "DSP_1",  "DSP_2",  "DIG",    "DSP_3",  "DSP_4",  "DSP_5",
    "DSP_6",  "DSP_7",  "DSP_8",  "DSP_9",  "DSP_10", "DSP_11", "DIG",    "DSP_12",
    "DIA",    "MES",    "ANO",    "HOR",    "MIN",    "SEG",    "DSP_13", "LIBRE",
]

# Word 10 sub-conmutada (Formato 2)
FMT2_WORD10 = [
    "DIG",    "ANA_10", "ANA_11", "ANA_12", "ANA_13", "ANA_14", "ANA_15", "ANA_16",
    "ANA_17", "ANA_18", "DIG",    "ANA_19", "ANA_20", "ANA_21", "ANA_22", "ANA_23",
    "ANA_24", "DIG",    "DSP_1",  "DSP_2",  "DIG",    "DSP_3",  "DSP_4",  "DSP_5",
    "DSP_6",  "DSP_7",  "DSP_8",  "DSP_9",  "DSP_10", "DSP_11", "DIG",    "DSP_12",
    "DIA",    "MES",    "ANO",    "HOR",    "MIN",    "SEG",    "LIBRE",  "LIBRE",
]
# Word 11 sub-conmutada (Formato 2) — incluye GPS
FMT2_WORD11 = [
    "ANA_26", "ANA_27", "ANA_28", "ANA_29", "ANA_30", "ANA_31", "ANA_32",
    "LAT_H",  "LAT_L",  "LON_H",  "LON_L",  "ALT",    "DSP_13",
    "LIBRE",  "LIBRE",  "LIBRE",  "LIBRE",  "LIBRE",  "LIBRE",  "LIBRE",
    "LIBRE",  "LIBRE",  "LIBRE",  "LIBRE",  "LIBRE",  "LIBRE",  "LIBRE",
    "LIBRE",  "LIBRE",  "LIBRE",  "LIBRE",  "LIBRE",  "LIBRE",  "LIBRE",
    "LIBRE",  "LIBRE",  "LIBRE",  "LIBRE",  "LIBRE",  "LIBRE",  "LIBRE",
]

# Formato 3: 10 frames × 6 words — incluye GPS y DSP_13
FMT3_TABLE = [
    ["DIG", "ANA_1",  "ANA_20", "ANA_30", "DSP_8",  "DIA"],
    ["DIG", "ANA_2",  "ANA_21", "ANA_31", "DSP_9",  "MES"],
    ["DIG", "ANA_3",  "ANA_22", "ANA_32", "DSP_10", "ANO"],
    ["DIG", "ANA_4",  "ANA_23", "DSP_1",  "DSP_11", "HOR"],
    ["DIG", "ANA_5",  "ANA_24", "DSP_2",  "DSP_12", "MIN"],
    ["DIG", "ANA_6",  "ANA_25", "DSP_3",  "LAT_H",  "SEG"],
    ["DIG", "ANA_7",  "ANA_26", "DSP_4",  "LAT_L",  "DSP_13"],
    ["DIG", "ANA_8",  "ANA_27", "DSP_5",  "LON_H",  "LIBRE"],
    ["DIG", "ANA_9",  "ANA_28", "DSP_6",  "LON_L",  "LIBRE"],
    ["DIG", "ANA_10", "ANA_29", "DSP_7",  "ALT",    "LIBRE"],
]

# ==============================================================================
# Metadatos IMU
# ==============================================================================
IMU_SCALES = {
    "DSP_1":  500,   "DSP_2":  500,   "DSP_3":  500,
    "DSP_4":  100,   "DSP_5":  100,   "DSP_6":  100,
    "DSP_7": 10000,  "DSP_8": 10000,  "DSP_9": 10000,
    "DSP_10": 100,   "DSP_11": 100,   "DSP_12": 1,
    "DSP_13": 100,   # Yaw (guiñada): 1 LSB = 0.01°
}
IMU_UNITS = {
    "DSP_1": "m/s²", "DSP_2": "m/s²", "DSP_3": "m/s²",
    "DSP_4": "°/s",  "DSP_5": "°/s",  "DSP_6": "°/s",
    "DSP_7": "a.u.", "DSP_8": "a.u.", "DSP_9": "a.u.",
    "DSP_10": "°",   "DSP_11": "°",   "DSP_12": "—",
    "DSP_13": "°",   # Yaw
}
IMU_LABELS = {
    "DSP_1": "Acc X",  "DSP_2": "Acc Y",  "DSP_3": "Acc Z",
    "DSP_4": "Gyr X",  "DSP_5": "Gyr Y",  "DSP_6": "Gyr Z",
    "DSP_7": "Mag X",  "DSP_8": "Mag Y",  "DSP_9": "Mag Z",
    "DSP_10": "Roll",  "DSP_11": "Pitch", "DSP_12": "PPS Sync",
    "DSP_13": "Yaw (Guiñada)",
}

# ==============================================================================
# Catálogo de sensores típicos de aeronave (sensor → unidad por defecto)
# ==============================================================================
SENSOR_CATALOG = {
    "Presión estática":             "hPa",
    "Presión dinámica (Pitot)":     "hPa",
    "Presión de aceite":            "kPa",
    "Presión de combustible":       "kPa",
    "Presión hidráulica":           "bar",
    "Temperatura exterior (OAT)":   "°C",
    "Temperatura de aceite":        "°C",
    "Temperatura culata (CHT)":     "°C",
    "Temperatura gases escape (EGT)": "°C",
    "RPM motor":                    "RPM",
    "RPM hélice":                   "RPM",
    "Posición de alerón":           "°",
    "Posición de elevador":         "°",
    "Posición de timón":            "°",
    "Posición de flap":             "°",
    "Posición de trim":             "°",
    "Ángulo de ataque (AOA)":       "°",
    "Fuerza en comando":            "N",
    "Galga extensiométrica":        "µε",
    "Acelerómetro estructural":     "g",
    "Caudal de combustible":        "L/h",
    "Nivel de combustible":         "L",
    "Tensión de bus":               "V",
    "Corriente de bus":             "A",
    "Velocidad indicada (IAS)":     "kt",
    "Velocidad vertical (VSI)":     "ft/min",
    "Altitud barométrica":          "ft",
    "Otro / personalizado":         "",
}

ANA_COLS     = [f"ANA_{i}" for i in range(1, 33)]
DIG_COLS     = [f"DIG_{i}" for i in range(1, 17)]
DSP_COLS     = [f"DSP_{i}" for i in range(1, 14)]   # DSP_1 … DSP_13
TIME_COLS    = ["DIA", "MES", "ANO", "HOR", "MIN", "SEG"]
GNSS_POS_COLS = ["LAT", "LON", "ALT"]               # GPS position (reconstruidos)
CSV_COLUMNS  = (["ciclo", "tiempo_s"] + ANA_COLS + DIG_COLS +
                DSP_COLS + TIME_COLS + GNSS_POS_COLS)

# Mapa CMD 1 (debe coincidir con cmd_port.c)
CMD_CH_COUNT = 72
CMD_CH_DSP_1 = 48
CMD_CH_DIA   = 61
CMD_CH_LAT_H = 67
CMD_CH_LAT_L = 68
CMD_CH_LON_H = 69
CMD_CH_LON_L = 70
CMD_CH_ALT   = 71

TIME_LABELS = {
    "DIA": "Día GNSS",
    "MES": "Mes GNSS",
    "ANO": "Año GNSS",
    "HOR": "Hora GNSS",
    "MIN": "Minuto GNSS",
    "SEG": "Segundo GNSS",
}

# ==============================================================================
# Detección de unidades removibles (Windows)
# ==============================================================================

DRIVE_REMOVABLE = 2   # pendrive, lector SD externo
DRIVE_FIXED     = 3   # disco fijo (también buscar aquí si queremos)

def get_removable_drives() -> list[str]:
    """Devuelve lista de letras de unidades removibles conectadas (ej: ['D:\\', 'E:\\'])."""
    drives = []
    try:
        # GetLogicalDrives() devuelve un bitmask de 26 bits (A-Z)
        bitmask = ctypes.windll.kernel32.GetLogicalDrives()
        for i, letter in enumerate(string.ascii_uppercase):
            if bitmask & (1 << i):
                path = f"{letter}:\\"
                drive_type = ctypes.windll.kernel32.GetDriveTypeW(path)
                if drive_type == DRIVE_REMOVABLE:
                    drives.append(path)
    except Exception:
        pass
    return drives


def find_fdr_files(drives: list[str]) -> list[dict]:
    """
    Busca archivos FDR_*.bin en las unidades dadas.
    Devuelve lista de dicts con keys: path, drive, name, size_kb.
    """
    found = []
    for drive in drives:
        try:
            for entry in os.scandir(drive):
                name = entry.name
                if name.upper().startswith("FDR_") and name.upper().endswith(".BIN"):
                    size_kb = entry.stat().st_size / 1024
                    found.append({
                        "path":     entry.path,
                        "drive":    drive,
                        "name":     name,
                        "size_kb":  size_kb,
                    })
        except (PermissionError, OSError):
            pass
    # Ordenar por nombre para que salgan FDR_0001, FDR_0002, etc.
    found.sort(key=lambda x: x["name"])
    return found


# ==============================================================================
# Parser del archivo .bin
# ==============================================================================

def _calc_crc32(data: bytes) -> int:
    return binascii.crc32(data) & 0xFFFFFFFF


def _reconstruct_latlon(h_val, l_val):
    """Reconstruye latitud o longitud (grados) desde dos int16 (parte alta y baja)."""
    if h_val is None or l_val is None:
        return None
    raw = (int(h_val) << 16) | (int(l_val) & 0xFFFF)
    # Convertir a int32 con signo
    if raw >= 0x80000000:
        raw -= 0x100000000
    return raw / 1_000_000.0


def _decode_frame(words: list, frame_id: int, fmt: int, params: dict) -> None:
    if fmt == 1:
        for i in range(16):
            params[f"ANA_{i+1}"] = words[i]
        pname = FMT1_WORD17[frame_id]
        if pname != "LIBRE":
            params[pname] = words[16]
    elif fmt == 2:
        for i in range(9):
            params[f"ANA_{i+1}"] = words[i]
        p10 = FMT2_WORD10[frame_id]
        if p10 != "LIBRE":
            params[p10] = words[9]
        p11 = FMT2_WORD11[frame_id]
        if p11 != "LIBRE":
            params[p11] = words[10]
    elif fmt == 3:
        for slot, pname in enumerate(FMT3_TABLE[frame_id]):
            if pname != "LIBRE":
                params[pname] = words[slot]


class _ArrayTable:
    """Vista liviana de los arrays. len() y el último ciclo, sin un dict por muestra."""

    def __init__(self, arrays: dict):
        self.arrays = arrays
        self._n = int(len(arrays["tiempo_s"])) if "tiempo_s" in arrays else 0

    def __len__(self):
        return self._n

    def __bool__(self):
        return self._n > 0

    def __getitem__(self, idx):
        if isinstance(idx, int) and idx < 0:
            idx = self._n + idx
        row = {}
        for key, col in self.arrays.items():
            val = float(col[idx])
            row[key] = None if np.isnan(val) else val
        return row


def _assign_subcomm(frames, word, table, cols):
    """Último valor del ciclo gana, igual que el dict del parser anterior."""
    n_frames = frames.shape[1]
    for fid, name in enumerate(table):
        if fid >= n_frames or name == "LIBRE":
            continue
        cols[name] = frames[:, fid, word]


def _latlon_col(high, low):
    h = high.astype(np.int64)
    l = low.astype(np.int64) & 0xFFFF
    raw = (h << 16) | l
    raw = np.where(raw >= 0x80000000, raw - 0x100000000, raw)
    out = raw.astype(np.float64) / 1_000_000.0
    missing = np.isnan(high) | np.isnan(low)
    out[missing] = np.nan
    return out.astype(np.float32)


def _parse_fdr_data(f, file_size, cev_format, data_offset, progress_cb=None):
    """Lee el .bin por bloques y devuelve columnas numpy, una por ciclo CEV."""
    if cev_format == 1:
        frames_per_cycle, words_per_frame, cycles_per_sec = 40, 17, 75
        n_fast = 16
    elif cev_format == 2:
        frames_per_cycle, words_per_frame, cycles_per_sec = 40, 11, 75
        n_fast = 9
    else:
        frames_per_cycle, words_per_frame, cycles_per_sec = 10, 6, 10
        n_fast = 0

    data_bytes = file_size - data_offset
    words_per_cycle = frames_per_cycle * words_per_frame
    bytes_per_cycle = words_per_cycle * 2
    n_cycles = data_bytes // bytes_per_cycle
    if n_cycles < 1:
        return {}

    if progress_cb:
        progress_cb(0, "Iniciando lectura…")

    # Pre-reservar arrays por columna (ocupan ~175 MB para 3 hs de vuelo en lugar de 1 GB contiguo)
    arrays = {k: np.full(n_cycles, np.nan, dtype=np.float32) for k in CSV_COLUMNS}
    arrays["ciclo"] = np.arange(1, n_cycles + 1, dtype=np.float32)
    arrays["tiempo_s"] = np.arange(n_cycles, dtype=np.float32) / cycles_per_sec

    f.seek(data_offset)
    CHUNK_CYCLES = 25000  # Bloques de ~34 MB para procesamiento eficiente sin sobrecargar memoria
    last_pct = -1

    for c_start in range(0, n_cycles, CHUNK_CYCLES):
        c_count = min(CHUNK_CYCLES, n_cycles - c_start)
        c_end = c_start + c_count

        raw_bytes = f.read(c_count * bytes_per_cycle)
        if len(raw_bytes) < c_count * bytes_per_cycle:
            actual_cycles = len(raw_bytes) // bytes_per_cycle
            if actual_cycles == 0:
                if c_start < n_cycles:
                    for k in arrays:
                        arrays[k] = arrays[k][:c_start]
                break
            c_count = actual_cycles
            c_end = c_start + c_count
            raw_bytes = raw_bytes[:actual_cycles * bytes_per_cycle]

        raw_chunk = np.frombuffer(raw_bytes, dtype="<i2")
        frames = raw_chunk.reshape(c_count, frames_per_cycle, words_per_frame)

        # 1. Canales rápidos
        if n_fast:
            fast = frames[:, :, :n_fast].mean(axis=1, dtype=np.float32)
            for i in range(n_fast):
                arrays[f"ANA_{i+1}"][c_start:c_end] = fast[:, i]

        # 2. Sub-conmutación
        chunk_cols = {}
        if cev_format == 1:
            _assign_subcomm(frames, 16, FMT1_WORD17, chunk_cols)
        elif cev_format == 2:
            _assign_subcomm(frames, 9, FMT2_WORD10, chunk_cols)
            _assign_subcomm(frames, 10, FMT2_WORD11, chunk_cols)
        else:
            for slot in range(words_per_frame):
                names = [row[slot] if slot < len(row) else "LIBRE" for row in FMT3_TABLE]
                _assign_subcomm(frames, slot, names, chunk_cols)

        # 3. Escalar IMU
        for key, scale in IMU_SCALES.items():
            if key in chunk_cols:
                chunk_cols[key] = chunk_cols[key].astype(np.float32) / scale

        # 4. Asignar a arrays principales
        for key, col in chunk_cols.items():
            if key in ("LAT_H", "LAT_L", "LON_H", "LON_L"):
                continue
            if key == "ALT":
                arrays["ALT"][c_start:c_end] = col.astype(np.float32) * np.float32(0.3048)
                continue
            if key == "DIG":
                dig = col.astype(np.int32)
                for bit in range(16):
                    arrays[f"DIG_{bit+1}"][c_start:c_end] = ((dig >> bit) & 1).astype(np.float32)
                continue
            if key in arrays:
                arrays[key][c_start:c_end] = col.astype(np.float32)

        # 5. GPS LAT / LON
        if "LAT_H" in chunk_cols and "LAT_L" in chunk_cols:
            arrays["LAT"][c_start:c_end] = _latlon_col(chunk_cols["LAT_H"], chunk_cols["LAT_L"])
        if "LON_H" in chunk_cols and "LON_L" in chunk_cols:
            arrays["LON"][c_start:c_end] = _latlon_col(chunk_cols["LON_H"], chunk_cols["LON_L"])

        if progress_cb:
            pct = min(99, int(100 * c_end / n_cycles))
            if pct != last_pct:
                mb_proc = (c_end * bytes_per_cycle) / (1024 * 1024)
                mb_tot = (n_cycles * bytes_per_cycle) / (1024 * 1024)
                progress_cb(pct, f"Procesando archivo… {mb_proc:.0f} / {mb_tot:.0f} MB ({pct}%)")
                last_pct = pct

    if progress_cb:
        progress_cb(100, f"Listo — {n_cycles:,} ciclos")
    return arrays


def parse_fdr_file(filepath: str, progress_cb=None, fallback_format: int = None):
    """
    Parsea FDR_XXXX.bin → (cev_format, records).

    Si el archivo tiene el header FDR válido, lo usa automáticamente.
    Si no (firmware viejo), usa fallback_format (1, 2 o 3) indicado por el usuario
    y lee desde el byte 0.
    """
    file_size = os.path.getsize(filepath)

    with open(filepath, "rb") as f:
        raw_header = f.read(FDR_FILE_HEADER_SIZE)

        has_valid_header = False
        cev_format = None

        if len(raw_header) >= FDR_FILE_HEADER_SIZE:
            magic, version, fmt_hdr, reserved, file_crc = HEADER_STRUCT.unpack(raw_header)
            if magic == FDR_FILE_MAGIC:
                if _calc_crc32(raw_header[:8]) != file_crc:
                    raise ValueError("CRC del header inválido. El archivo puede estar corrompido.")
                if fmt_hdr not in (1, 2, 3):
                    raise ValueError(f"Formato CEV inválido en el header: {fmt_hdr}")
                has_valid_header = True
                cev_format = fmt_hdr

        if not has_valid_header:
            if fallback_format is None:
                raise _NoHeaderError(
                    "El archivo no tiene el header FDR del firmware actualizado.\n"
                    "Seleccioná el formato CEV manualmente para continuar."
                )
            cev_format = fallback_format

        data_offset = FDR_FILE_HEADER_SIZE if has_valid_header else 0
        arrays = _parse_fdr_data(f, file_size, cev_format, data_offset, progress_cb)

    return cev_format, _ArrayTable(arrays)


class _NoHeaderError(Exception):
    """Archivo sin header FDR — requiere que el usuario elija el formato."""
    pass


def export_csv(records, output_path: str) -> None:
    if isinstance(records, _ArrayTable):
        keys = [k for k in CSV_COLUMNS if k in records.arrays]
        n = len(records)
        with open(output_path, "w", newline="", encoding="utf-8") as f:
            writer = csv.writer(f)
            writer.writerow(keys)
            step = 20000
            for start in range(0, n, step):
                end = min(n, start + step)
                block = np.column_stack([records.arrays[k][start:end] for k in keys])
                for row in block:
                    writer.writerow(["" if np.isnan(v) else v for v in row])
        return
    with open(output_path, "w", newline="", encoding="utf-8") as f:
        writer = csv.DictWriter(f, fieldnames=CSV_COLUMNS, extrasaction="ignore")
        writer.writeheader()
        for rec in records:
            writer.writerow({k: ("" if v is None else v) for k, v in rec.items()})


# ==============================================================================
# Análisis espectral — PSD por método de Welch (sin scipy)
# ==============================================================================

def welch_psd(x: np.ndarray, fs: float,
              nperseg: int = 1024,
              noverlap: int | None = None,
              window: str = "hann") -> tuple[np.ndarray, np.ndarray]:
    """
    Calcula la densidad espectral de potencia (PSD) con el método de Welch.
    Devuelve (freqs_hz, psd) donde psd tiene unidades de [x_unit²/Hz].
    """
    x = x[~np.isnan(x)]
    n = len(x)
    if n < 4:
        return np.array([0.0]), np.array([0.0])

    nperseg = min(nperseg, n)
    if noverlap is None:
        noverlap = nperseg // 2
    step = nperseg - noverlap

    if window == "hann":
        win = np.hanning(nperseg)
    elif window == "hamming":
        win = np.hamming(nperseg)
    elif window == "blackman":
        win = np.blackman(nperseg)
    else:
        win = np.ones(nperseg)

    win_norm = np.dot(win, win)
    freqs    = np.fft.rfftfreq(nperseg, d=1.0 / fs)
    psd      = np.zeros(len(freqs))
    count    = 0

    for start in range(0, n - nperseg + 1, step):
        seg      = (x[start: start + nperseg] - x[start: start + nperseg].mean()) * win
        fft_seg  = np.fft.rfft(seg)
        psd     += np.abs(fft_seg) ** 2 / (fs * win_norm)
        count   += 1

    if count > 0:
        psd /= count
    psd[1:-1] *= 2   # espectro unilateral

    return freqs, psd


def magnitude_spectrum(x: np.ndarray, fs: float, max_n: int = 32768):
    """FFT unilateral |Y(f)|, sin DC. Devuelve (freqs_hz, magnitud)."""
    y = np.asarray(x, dtype=np.float64)
    y = y[~np.isnan(y)]
    if len(y) < 8:
        return np.array([]), np.array([])
    if len(y) > max_n:
        y = y[-max_n:]
    y = y - y.mean()
    n = len(y)
    mag = np.abs(np.fft.rfft(y)) * 2.0 / n
    freqs = np.fft.rfftfreq(n, d=1.0 / fs)
    return freqs[1:], mag[1:]


# ==============================================================================
# Límite de puntos para gráficos (decimación automática)
# ==============================================================================
MAX_PLOT_POINTS = 25000   # puntos máximos por canal en los gráficos generales


def _build_arrays(records: list) -> dict:
    """
    Convierte la lista de dicts en un dict de numpy arrays (float32) para
    acceso rápido. Una sola pasada; None → NaN.
    """
    if not records:
        return {}
    n = len(records)
    keys = list(records[0].keys())
    out = {k: np.empty(n, dtype=np.float32) for k in keys}
    for i, r in enumerate(records):
        for k in keys:
            v = r.get(k)
            out[k][i] = np.nan if v is None else v
    return out


def _decimate(arr: np.ndarray, step: int) -> np.ndarray:
    return arr[::step] if step > 1 else arr


# ==============================================================================
# Paleta de colores
# ==============================================================================
BG_DARK  = "#1e1e2e"
BG_PANEL = "#2a2a3e"
BG_CARD  = "#313147"
FG_TEXT  = "#cdd6f4"
FG_MUTED = "#7f849c"
ACCENT   = "#89b4fa"
GREEN    = "#a6e3a1"
RED      = "#f38ba8"
YELLOW   = "#f9e2af"
ORANGE   = "#fab387"


def _style_ttk(root):
    s = ttk.Style(root)
    s.theme_use("clam")
    s.configure(".",                   background=BG_DARK,  foreground=FG_TEXT, font=("Segoe UI", 10))
    s.configure("TFrame",              background=BG_DARK)
    s.configure("TLabel",              background=BG_DARK,  foreground=FG_TEXT)
    s.configure("TButton",             background=BG_PANEL, foreground=FG_TEXT, borderwidth=0, relief="flat", padding=6)
    s.map(       "TButton",            background=[("active", BG_CARD)])
    s.configure("Accent.TButton",      background=ACCENT,   foreground="#1e1e2e", font=("Segoe UI", 10, "bold"))
    s.map(       "Accent.TButton",     background=[("active", "#74a8e8")])
    s.configure("TNotebook",           background=BG_DARK,  borderwidth=0)
    s.configure("TNotebook.Tab",       background=BG_PANEL, foreground=FG_MUTED, padding=[12, 6])
    s.map(       "TNotebook.Tab",      background=[("selected", BG_CARD)],
                                       foreground=[("selected", FG_TEXT)])
    s.configure("Horizontal.TProgressbar", background=ACCENT, troughcolor=BG_PANEL, borderwidth=0)
    s.configure("Treeview",            background=BG_PANEL, foreground=FG_TEXT,
                                       fieldbackground=BG_PANEL, borderwidth=0,
                                       rowheight=28, font=("Segoe UI", 9))
    s.configure("Treeview.Heading",    background=BG_CARD,  foreground=FG_MUTED,
                                       font=("Segoe UI", 8, "bold"), relief="flat")
    s.map(       "Treeview",           background=[("selected", ACCENT)],
                                       foreground=[("selected", "#1e1e2e")])
    s.configure("TScrollbar",         background=BG_PANEL, troughcolor=BG_DARK, borderwidth=0)


# ==============================================================================
# GUI principal
# ==============================================================================

class FDRConverterApp(tk.Tk):
    def __init__(self):
        super().__init__()
        self.title("FDR Converter  ·  CEV-FAA")
        self.geometry("1120x740")
        self.minsize(900, 620)
        self.configure(bg=BG_DARK)
        _style_ttk(self)

        self._bin_path    = tk.StringVar()
        self._csv_path    = tk.StringVar()
        self._status_text = tk.StringVar(value="Listo. Conectá el pendrive o seleccioná un archivo .bin.")
        self._progress    = tk.IntVar(value=0)
        self._records     = None
        self._cev_format  = None
        self._found_files : list[dict] = []
        self._ch_var      = tk.StringVar()   # canal seleccionado en Vista Individual
        self._cal_mode    = tk.BooleanVar(value=False)   # True = mostrar valores calibrados
        self._freq_mode   = tk.BooleanVar(value=False)   # True = dominio de la frecuencia
        self._time_unit   = tk.StringVar(value="Segundos (s)")
        # cal_data: {canal: {"name", "unit", "mode": "linear"|"poly", "coeffs": [...]}}
        self._cal_data: dict = {}
        self._grid_cfg: dict = {}

        self._build_ui()

        # Escaneo automático al iniciar
        self.after(400, self._auto_scan)

        # Cierre limpio (detener monitor RS-232 si está activo)
        self.protocol("WM_DELETE_WINDOW", self._on_close)

    def _on_close(self):
        if getattr(self, "_mon_running", False):
            self._mon_stop()
        self.destroy()

    # ──────────────────────────────────────────────────────────────────
    # Construcción de la UI
    # ──────────────────────────────────────────────────────────────────
    def _build_ui(self):
        # Header
        hdr = tk.Frame(self, bg=BG_PANEL, height=52)
        hdr.pack(fill="x", side="top")
        tk.Label(hdr, text="✈  FDR Converter",
                 bg=BG_PANEL, fg=ACCENT, font=("Segoe UI", 14, "bold")).pack(side="left", padx=20, pady=10)
        tk.Label(hdr, text="CEV-FAA / IUA",
                 bg=BG_PANEL, fg=FG_MUTED, font=("Segoe UI", 10)).pack(side="left", pady=10)

        # Body
        body = tk.Frame(self, bg=BG_DARK)
        body.pack(fill="both", expand=True, padx=12, pady=(8, 0))

        # Sidebar izquierdo con scroll para pantallas chicas
        left_container = tk.Frame(body, bg=BG_DARK, width=320)
        left_container.pack(side="left", fill="y", padx=(0, 10))
        left_container.pack_propagate(False)

        canvas = tk.Canvas(left_container, bg=BG_DARK, highlightthickness=0)
        scrollbar = ttk.Scrollbar(left_container, orient="vertical", command=canvas.yview)
        left = tk.Frame(canvas, bg=BG_DARK)

        left.bind("<Configure>", lambda e: canvas.configure(scrollregion=canvas.bbox("all")))
        canvas.create_window((0, 0), window=left, anchor="nw", width=305)
        canvas.configure(yscrollcommand=scrollbar.set)

        scrollbar.pack(side="right", fill="y")
        canvas.pack(side="left", fill="both", expand=True)

        def _on_mousewheel(event):
            canvas.yview_scroll(int(-1*(event.delta/120)), "units")
        left_container.bind("<Enter>", lambda _: canvas.bind_all("<MouseWheel>", _on_mousewheel))
        left_container.bind("<Leave>", lambda _: canvas.unbind_all("<MouseWheel>"))

        self._build_device_panel(left)
        self._build_file_panel(left)
        self._build_actions_panel(left)
        self._build_info_panel(left)

        # Panel de gráficos derecho
        right = tk.Frame(body, bg=BG_DARK)
        right.pack(side="left", fill="both", expand=True)
        self._notebook = ttk.Notebook(right)
        self._notebook.pack(fill="both", expand=True)

        self._tab_ana  = ttk.Frame(self._notebook)
        self._tab_imu  = ttk.Frame(self._notebook)
        self._tab_dig  = ttk.Frame(self._notebook)
        self._tab_time = ttk.Frame(self._notebook)
        self._tab_gps  = ttk.Frame(self._notebook)
        self._tab_indv = ttk.Frame(self._notebook)
        self._tab_psd  = ttk.Frame(self._notebook)
        self._tab_cfg  = ttk.Frame(self._notebook)
        self._tab_mon  = ttk.Frame(self._notebook)

        self._notebook.add(self._tab_ana,  text="  Señales Analógicas  ")
        self._notebook.add(self._tab_imu,  text="  IMU / Actitud        ")
        self._notebook.add(self._tab_dig,  text="  Señales Discretas    ")
        self._notebook.add(self._tab_time, text="  Tiempo GNSS          ")
        self._notebook.add(self._tab_gps,  text="  🌍 Posición GPS       ")
        self._notebook.add(self._tab_indv, text="  🔍 Vista Individual   ")
        self._notebook.add(self._tab_psd,  text="  📊 PSD / Vibración    ")
        self._notebook.add(self._tab_cfg,  text="  ⚙ Configuración       ")
        self._notebook.add(self._tab_mon,  text="  📡 Monitor RS-232     ")

        for tab in (self._tab_ana, self._tab_imu, self._tab_dig,
                    self._tab_time, self._tab_gps, self._tab_indv, self._tab_psd):
            self._placeholder(tab, "Seleccioná un archivo y presioná  ▶ Convertir y Graficar")

        # Pestañas siempre disponibles (no requieren archivo cargado)
        self._build_config_tab()
        self._build_monitor_tab()

        # Status bar
        bar = tk.Frame(self, bg=BG_PANEL, height=34)
        bar.pack(fill="x", side="bottom")
        ttk.Progressbar(bar, variable=self._progress, maximum=100,
                        style="Horizontal.TProgressbar", length=320).pack(
                            side="left", padx=12, pady=7, fill="x")
        tk.Label(bar, textvariable=self._status_text,
                 bg=BG_PANEL, fg=FG_MUTED, font=("Segoe UI", 9)).pack(side="left")

    # ── Sección: detección de dispositivos ────────────────────────────
    def _build_device_panel(self, parent):
        card = self._card(parent, "Dispositivo")

        # Fila de botones
        btn_row = tk.Frame(card, bg=BG_CARD)
        btn_row.pack(fill="x", padx=10, pady=(0, 6))
        ttk.Button(btn_row, text="🔄  Detectar dispositivo",
                   command=self._scan_devices).pack(side="left", fill="x", expand=True)

        # Tabla de archivos encontrados
        tree_frame = tk.Frame(card, bg=BG_CARD)
        tree_frame.pack(fill="x", padx=10, pady=(0, 10))

        cols = ("name", "drive", "size")
        self._tree = ttk.Treeview(tree_frame, columns=cols, show="headings", height=5, selectmode="browse")
        self._tree.heading("name",  text="Archivo")
        self._tree.heading("drive", text="Unidad")
        self._tree.heading("size",  text="Tamaño")
        self._tree.column("name",  width=130, anchor="w")
        self._tree.column("drive", width=60,  anchor="center")
        self._tree.column("size",  width=75,  anchor="e")

        sb = ttk.Scrollbar(tree_frame, orient="vertical", command=self._tree.yview)
        self._tree.configure(yscrollcommand=sb.set)
        self._tree.pack(side="left", fill="x", expand=True)
        sb.pack(side="right", fill="y")

        self._tree.bind("<<TreeviewSelect>>", self._on_file_selected)
        self._tree.bind("<Double-1>",         self._on_file_double_click)

        # Etiqueta de ayuda
        self._device_hint = tk.Label(card, text="Conectá el pendrive y presioná Detectar",
                                     bg=BG_CARD, fg=FG_MUTED, font=("Segoe UI", 8),
                                     wraplength=270)
        self._device_hint.pack(anchor="w", padx=10, pady=(0, 10))

    # ── Sección: archivo manual ────────────────────────────────────────
    def _build_file_panel(self, parent):
        card = self._card(parent, "Selección manual")

        tk.Label(card, text="Archivo .bin:",
                 bg=BG_CARD, fg=FG_TEXT, font=("Segoe UI", 9)).pack(anchor="w", padx=10, pady=(0, 2))
        row = tk.Frame(card, bg=BG_CARD)
        row.pack(fill="x", padx=10, pady=(0, 8))
        tk.Entry(row, textvariable=self._bin_path, bg=BG_PANEL, fg=FG_TEXT,
                 insertbackground=FG_TEXT, relief="flat",
                 font=("Segoe UI", 9), width=22).pack(side="left", fill="x", expand=True)
        ttk.Button(row, text="…", width=3, command=self._browse_bin).pack(side="left", padx=(4, 0))

        tk.Label(card, text="Guardar CSV en:",
                 bg=BG_CARD, fg=FG_TEXT, font=("Segoe UI", 9)).pack(anchor="w", padx=10, pady=(0, 2))
        row2 = tk.Frame(card, bg=BG_CARD)
        row2.pack(fill="x", padx=10, pady=(0, 10))
        tk.Entry(row2, textvariable=self._csv_path, bg=BG_PANEL, fg=FG_TEXT,
                 insertbackground=FG_TEXT, relief="flat",
                 font=("Segoe UI", 9), width=22).pack(side="left", fill="x", expand=True)
        ttk.Button(row2, text="…", width=3, command=self._browse_csv).pack(side="left", padx=(4, 0))

    # ── Sección: acciones ──────────────────────────────────────────────
    def _build_actions_panel(self, parent):
        card = self._card(parent, "Acciones")
        ttk.Button(card, text="▶  Convertir y Graficar",
                   style="Accent.TButton",
                   command=self._start_conversion).pack(fill="x", padx=10, pady=(0, 6))
        ttk.Button(card, text="💾  Guardar CSV",
                   command=self._save_csv).pack(fill="x", padx=10, pady=(0, 6))
        ttk.Button(card, text="🔧  Calibración de canales",
                   command=self._open_cal_dialog).pack(fill="x", padx=10, pady=(0, 6))
        ttk.Button(card, text="🖼  Exportar gráfico activo",
                   command=self._export_current_plot).pack(fill="x", padx=10, pady=(0, 6))
        row_btns = tk.Frame(card, bg=BG_CARD)
        row_btns.pack(fill="x", padx=10, pady=(0, 6))
        ttk.Button(row_btns, text="📄  Reporte PDF",
                   command=self._generate_report).pack(side="left", fill="x", expand=True, padx=(0, 3))
        ttk.Button(row_btns, text="🗑  Formatear SD",
                   command=self._format_sd_dialog).pack(side="left", fill="x", expand=True, padx=(3, 0))
        cal_row = tk.Frame(card, bg=BG_CARD)
        cal_row.pack(fill="x", padx=10, pady=(0, 10))
        tk.Checkbutton(cal_row, text="Mostrar valores calibrados",
                       variable=self._cal_mode,
                       command=self._on_cal_toggle,
                       bg=BG_CARD, fg=FG_TEXT, selectcolor=BG_PANEL,
                       activebackground=BG_CARD, activeforeground=ACCENT,
                       font=("Segoe UI", 9)).pack(side="left")
        freq_row = tk.Frame(card, bg=BG_CARD)
        freq_row.pack(fill="x", padx=10, pady=(0, 6))
        tk.Checkbutton(freq_row, text="Dominio de la frecuencia (FFT)",
                       variable=self._freq_mode,
                       command=self._on_cal_toggle,
                       bg=BG_CARD, fg=FG_TEXT, selectcolor=BG_PANEL,
                       activebackground=BG_CARD, activeforeground=ACCENT,
                       font=("Segoe UI", 9)).pack(side="left")
        time_row = tk.Frame(card, bg=BG_CARD)
        time_row.pack(fill="x", padx=10, pady=(0, 10))
        tk.Label(time_row, text="Tiempo:", bg=BG_CARD, fg=FG_TEXT,
                 font=("Segoe UI", 9)).pack(side="left", padx=(0, 6))
        self._time_unit_combo = ttk.Combobox(time_row, textvariable=self._time_unit,
                                             values=["Segundos (s)", "Minutos (min)", "Horas (h)"],
                                             state="readonly", font=("Segoe UI", 9), width=13)
        self._time_unit_combo.pack(side="left")
        self._time_unit_combo.bind("<<ComboboxSelected>>", self._on_time_unit_change)

    # ── Sección: info del archivo ──────────────────────────────────────
    def _build_info_panel(self, parent):
        card = self._card(parent, "Información del archivo")
        self._info_label = tk.Label(card, text="—", bg=BG_CARD, fg=FG_MUTED,
                                    font=("Segoe UI", 9), justify="left", wraplength=278)
        self._info_label.pack(anchor="w", padx=10, pady=(0, 10))

    # ──────────────────────────────────────────────────────────────────
    # Helpers UI
    # ──────────────────────────────────────────────────────────────────
    def _card(self, parent, title: str) -> tk.Frame:
        frame = tk.Frame(parent, bg=BG_CARD, bd=0)
        frame.pack(fill="x", pady=(0, 10))
        tk.Label(frame, text=title.upper(), bg=BG_CARD, fg=FG_MUTED,
                 font=("Segoe UI", 8, "bold")).pack(anchor="w", padx=10, pady=(8, 4))
        return frame

    def _placeholder(self, tab, msg: str):
        for w in tab.winfo_children():
            w.destroy()
        tk.Label(tab, text=msg, bg=BG_DARK, fg=FG_MUTED,
                 font=("Segoe UI", 11)).pack(expand=True)

    # ──────────────────────────────────────────────────────────────────
    # Detección de dispositivos
    # ──────────────────────────────────────────────────────────────────
    def _auto_scan(self):
        """Escaneo silencioso al arrancar."""
        drives = get_removable_drives()
        if drives:
            self._populate_tree(find_fdr_files(drives))

    def _scan_devices(self):
        """Botón Detectar — escanea y actualiza la tabla."""
        self._set_status("Escaneando unidades removibles…")
        drives = get_removable_drives()

        if not drives:
            self._device_hint.config(
                text="⚠  No se encontraron unidades removibles.\n"
                     "Conectá el pendrive y volvé a intentar.",
                fg=YELLOW,
            )
            self._set_status("No se encontraron unidades removibles.")
            return

        files = find_fdr_files(drives)
        self._populate_tree(files)

        drives_str = "  ".join(d.rstrip("\\") for d in drives)
        if files:
            self._device_hint.config(
                text=f"✓  {len(files)} archivo(s) encontrado(s) en {drives_str}.\n"
                     "Hacé doble clic o seleccioná y presioná Convertir.",
                fg=GREEN,
            )
            self._set_status(f"{len(files)} archivo(s) FDR encontrado(s) en {drives_str}.")
        else:
            self._device_hint.config(
                text=f"Unidad {drives_str} detectada pero sin archivos FDR_*.bin.",
                fg=YELLOW,
            )
            self._set_status(f"Sin archivos FDR en {drives_str}.")

    def _populate_tree(self, files: list[dict]):
        """Actualiza la Treeview con la lista de archivos."""
        for row in self._tree.get_children():
            self._tree.delete(row)
        self._found_files = files
        for f in files:
            size_str = f"{f['size_kb']:.0f} KB" if f["size_kb"] < 1024 else f"{f['size_kb']/1024:.1f} MB"
            self._tree.insert("", "end", iid=f["path"],
                              values=(f["name"], f["drive"].rstrip("\\"), size_str))

    def _on_file_selected(self, _event=None):
        sel = self._tree.selection()
        if not sel:
            return
        path = sel[0]  # iid = ruta completa
        self._bin_path.set(path)
        self._csv_path.set(str(Path(path).with_suffix(".csv")))

    def _on_file_double_click(self, _event=None):
        self._on_file_selected()
        self._start_conversion()

    # ──────────────────────────────────────────────────────────────────
    # Selección manual de archivos
    # ──────────────────────────────────────────────────────────────────
    def _browse_bin(self):
        path = filedialog.askopenfilename(
            title="Seleccionar archivo FDR",
            filetypes=[("Archivos FDR", "*.bin"), ("Todos", "*.*")],
        )
        if path:
            self._bin_path.set(path)
            self._csv_path.set(str(Path(path).with_suffix(".csv")))

    def _browse_csv(self):
        path = filedialog.asksaveasfilename(
            title="Guardar CSV como",
            defaultextension=".csv",
            filetypes=[("CSV", "*.csv"), ("Todos", "*.*")],
        )
        if path:
            self._csv_path.set(path)

    # ──────────────────────────────────────────────────────────────────
    # Conversión
    # ──────────────────────────────────────────────────────────────────
    def _start_conversion(self, fallback_format: int = None):
        bin_path = self._bin_path.get().strip()
        if not bin_path:
            messagebox.showwarning("Sin archivo", "Seleccioná un archivo .bin primero.")
            return
        if not os.path.isfile(bin_path):
            messagebox.showerror("Error", f"No se encontró el archivo:\n{bin_path}")
            return

        self._set_status("Leyendo archivo…", 0)
        self._records    = None
        self._arrays     = None
        self._cev_format = None

        def run():
            try:
                fmt, recs = parse_fdr_file(bin_path,
                                           progress_cb=self._ui_progress,
                                           fallback_format=fallback_format)
                self.after(0, lambda: self._on_done(
                    fmt, recs, bin_path, recs.arrays))
            except _NoHeaderError as exc:
                self.after(0, lambda e=exc: self._ask_format_then_convert(bin_path, str(e)))
            except Exception as exc:
                self.after(0, lambda e=exc: self._on_error(e))

        threading.Thread(target=run, daemon=True).start()

    def _ask_format_then_convert(self, bin_path: str, reason: str):
        """Muestra un diálogo para elegir el formato CEV cuando el archivo no tiene header."""
        dlg = tk.Toplevel(self)
        dlg.title("Formato no detectado")
        dlg.configure(bg=BG_DARK)
        dlg.resizable(False, False)
        dlg.grab_set()

        # Centrar sobre la ventana principal
        dlg.geometry("420x260")
        dlg.update_idletasks()
        x = self.winfo_x() + (self.winfo_width()  - 420) // 2
        y = self.winfo_y() + (self.winfo_height() - 260) // 2
        dlg.geometry(f"+{x}+{y}")

        tk.Label(dlg, text="⚠  Archivo sin header FDR", bg=BG_DARK, fg=YELLOW,
                 font=("Segoe UI", 11, "bold")).pack(pady=(18, 4))
        tk.Label(dlg, text=(
            "El archivo fue generado con el firmware anterior\n"
            "(sin header). Seleccioná el formato CEV que\n"
            "usaste para grabar este vuelo:"),
                 bg=BG_DARK, fg=FG_MUTED, font=("Segoe UI", 9),
                 justify="center").pack(pady=(0, 14))

        fmt_var = tk.IntVar(value=1)
        for val, desc in [
            (1, "Formato 1  —  40 frames × 17 words  (75 ciclos/s)"),
            (2, "Formato 2  —  40 frames × 11 words  (75 ciclos/s)"),
            (3, "Formato 3  —  10 frames × 6 words   (10 ciclos/s)"),
        ]:
            tk.Radiobutton(dlg, text=desc, variable=fmt_var, value=val,
                           bg=BG_DARK, fg=FG_TEXT, selectcolor=BG_PANEL,
                           activebackground=BG_DARK, activeforeground=ACCENT,
                           font=("Segoe UI", 9)).pack(anchor="w", padx=36)

        btn_row = tk.Frame(dlg, bg=BG_DARK)
        btn_row.pack(pady=16)

        def on_ok():
            chosen = fmt_var.get()
            dlg.destroy()
            self._start_conversion(fallback_format=chosen)

        def on_cancel():
            dlg.destroy()
            self._set_status("Conversión cancelada.", 0)

        ttk.Button(btn_row, text="Continuar", style="Accent.TButton",
                   command=on_ok).pack(side="left", padx=6)
        ttk.Button(btn_row, text="Cancelar",
                   command=on_cancel).pack(side="left", padx=6)

    def _ui_progress(self, pct: int, msg: str | None = None):
        text = msg or f"Procesando… {pct}%"
        self.after(0, lambda p=pct, t=text: (
            self._progress.set(p), self._status_text.set(t)))

    def _set_status(self, msg: str, pct: int | None = None):
        self._status_text.set(msg)
        if pct is not None:
            self._progress.set(pct)

    def _on_done(self, fmt, records, bin_path, arrays=None):
        self._cev_format = fmt
        self._records    = records
        self._arrays     = arrays if arrays is not None else _build_arrays(records)

        fmt_names = {
            1: "Formato 1  (40×17 words, 3000 fr/s)",
            2: "Formato 2  (40×11 words, 3000 fr/s)",
            3: "Formato 3  (10×6 words, 100 fr/s)",
        }
        cps      = {1: 75, 2: 75, 3: 10}[fmt]
        dur_s    = len(records) / cps
        size_mb  = os.path.getsize(bin_path) / (1024 * 1024)
        step     = max(1, len(records) // MAX_PLOT_POINTS)
        info     = (
            f"CEV {fmt_names[fmt]}\n"
            f"Ciclos:    {len(records):,}\n"
            f"Duración:  {dur_s:.1f} s  ({dur_s/60:.1f} min)\n"
            f"Archivo:   {size_mb:.1f} MB\n"
            + (f"Gráfico general: cada {step} ciclos\n(Vista Individual: 100% datos reales)" if step > 1 else "")
        )
        self._info_label.config(text=info, fg=GREEN)

        if self._csv_path.get().strip() and size_mb <= 40:
            self._save_csv(silent=True)

        extra = ""
        if size_mb > 40:
            extra = "  CSV no se generó solo: usá Guardar CSV."
        self._set_status(
            f"OK — {len(records):,} ciclos  ({dur_s:.0f} s). Graficando…{extra}", 90
        )
        # Diferir gráficos: evita congelar la UI justo al terminar el parse.
        self.after(20, lambda: self._draw_after_load(fmt))

    def _draw_after_load(self, fmt):
        records = self._records
        if not records:
            return
        try:
            self._draw_plots(records, fmt)
            self._setup_individual_tab(records, fmt)
            self._setup_psd_tab(records, fmt)
            self._draw_gps_tab(records)
            cps = {1: 75, 2: 75, 3: 10}[fmt]
            dur_s = len(records) / cps
            if dur_s >= 3600:
                dur_str = f"{dur_s/3600:.2f} h ({dur_s:.0f} s)"
            elif dur_s >= 60:
                dur_str = f"{dur_s/60:.1f} min ({dur_s:.0f} s)"
            else:
                dur_str = f"{dur_s:.0f} s"
            self._set_status(
                f"OK — {len(records):,} ciclos  ({dur_str} de vuelo)", 100
            )
        except Exception as exc:
            self._on_error(exc)

    def _on_error(self, exc):
        self._set_status(f"Error: {exc}", 0)
        messagebox.showerror("Error al leer el archivo", str(exc))

    # ──────────────────────────────────────────────────────────────────
    # Calibración — aplicación y toggle
    # ──────────────────────────────────────────────────────────────────
    def _apply_cal(self, arr: np.ndarray, key: str) -> np.ndarray:
        """Aplica los coeficientes de calibración a un array numpy. Sin efecto si no hay datos."""
        c = self._cal_data.get(key)
        if not c or not c.get("coeffs"):
            return arr
        return np.polyval(c["coeffs"], arr)

    def _cal_label(self, key: str, base_label: str) -> str:
        """Devuelve etiqueta con unidad calibrada si está disponible."""
        if not self._cal_mode.get():
            return base_label
        c = self._cal_data.get(key, {})
        name = c.get("name", "").strip()
        unit = self._channel_unit(key)
        if name:
            return f"{name}  [{unit}]" if unit else name
        return f"{base_label}  [{unit}]" if unit else base_label

    def _channel_unit(self, key: str) -> str:
        """Unidad del canal: calibración, grilla, IMU o LSB."""
        if self._cal_mode.get():
            u = (self._cal_data.get(key, {}).get("unit", "") or
                 self._grid_cfg.get(key, {}).get("unit", "")).strip()
            if u:
                return u
        if key in IMU_UNITS:
            return IMU_UNITS[key]
        if key.startswith("DIG"):
            return "0/1"
        if key in ("LAT", "LON"):
            return "°"
        if key == "ALT":
            return "m"
        if key in TIME_COLS:
            return TIME_LABELS.get(key, "")
        return "LSB"

    def _cal_ylabel(self, base: str, keys: list) -> str:
        """Unidad del eje Y. Con calibración activa usa la unidad elegida."""
        if not self._cal_mode.get():
            return base
        units = []
        for k in keys:
            u = (self._cal_data.get(k, {}).get("unit", "") or
                 self._grid_cfg.get(k, {}).get("unit", "")).strip()
            if u:
                units.append(u)
        uniq = list(dict.fromkeys(units))
        if len(uniq) == 1:
            return uniq[0]
        if uniq:
            return " / ".join(uniq)
        return base

    def _series_fs(self) -> float:
        return 10.0 if self._cev_format == 3 else 75.0

    def _get_time_scale_and_label(self):
        """Devuelve (factor_escala, etiqueta_eje, unidad_corta) según la unidad de tiempo elegida."""
        unit = self._time_unit.get() if hasattr(self, "_time_unit") else "Segundos (s)"
        if "Minuto" in unit or unit == "min":
            return 1.0 / 60.0, "Tiempo (min)", "min"
        elif "Hora" in unit or unit == "h":
            return 1.0 / 3600.0, "Tiempo (h)", "h"
        else:
            return 1.0, "Tiempo (s)", "s"

    def _on_time_unit_change(self, _event=None):
        if self._records:
            self._replot_all()

    def _replot_all(self):
        if not self._records:
            return
        fmt = self._cev_format
        self._draw_plots(self._records, fmt)
        self._draw_gps_tab(self._records)
        if hasattr(self, "_indv_combo") and self._indv_combo.winfo_exists():
            label = self._indv_combo.get()
            if label:
                self._draw_individual(label, self._records)
        if hasattr(self, "_fig_psd") and self._fig_psd is not None:
            self._calculate_psd()

    def _trace_xy(self, val, t):
        if self._freq_mode.get():
            return magnitude_spectrum(val, self._series_fs())
        return t, val

    def _on_cal_toggle(self):
        if self._records:
            self._replot_with_cal()

    def _replot_with_cal(self):
        self._replot_all()
        self._setup_individual_tab(self._records, self._cev_format)

    # ──────────────────────────────────────────────────────────────────
    # Guardar CSV
    # ──────────────────────────────────────────────────────────────────
    def _save_csv(self, silent=False):
        if not self._records:
            if not silent:
                messagebox.showwarning("Sin datos", "Primero convertí un archivo .bin.")
            return
        csv_path = self._csv_path.get().strip()
        if not csv_path:
            csv_path = filedialog.asksaveasfilename(defaultextension=".csv",
                                                    filetypes=[("CSV", "*.csv")])
            if not csv_path:
                return
            self._csv_path.set(csv_path)
        try:
            if self._cal_mode.get() and self._cal_data and isinstance(self._records, _ArrayTable):
                calibrated = _ArrayTable({k: v.copy() for k, v in self._records.arrays.items()})
                for key, c in self._cal_data.items():
                    if key in calibrated.arrays and c.get("coeffs"):
                        col = calibrated.arrays[key]
                        good = ~np.isnan(col)
                        col[good] = np.polyval(c["coeffs"], col[good]).astype(np.float32)
                export_csv(calibrated, csv_path)
                self._set_status(f"CSV calibrado guardado: {Path(csv_path).name}", 100)
            else:
                export_csv(self._records, csv_path)
                self._set_status(f"CSV guardado: {Path(csv_path).name}", 100)
        except Exception as exc:
            messagebox.showerror("Error al guardar", str(exc))

    # ──────────────────────────────────────────────────────────────────
    # Gráficos
    # ──────────────────────────────────────────────────────────────────
    def _clear_tab(self, tab):
        for w in tab.winfo_children():
            w.destroy()

    def _embed_figure(self, fig: Figure, tab):
        canvas = FigureCanvasTkAgg(fig, master=tab)
        canvas.draw()
        tb = NavigationToolbar2Tk(canvas, tab, pack_toolbar=False)
        tb.update()
        tb.pack(side="bottom", fill="x")
        canvas.get_tk_widget().pack(fill="both", expand=True)
        return canvas

    # ──────────────────────────────────────────────────────────────────
    # Leyenda interactiva: click para mostrar/ocultar canal
    # ──────────────────────────────────────────────────────────────────
    def _make_legend_interactive(self, canvas, fig: Figure, lined: dict):
        """
        lined: {legend_line: data_line}
        Click en una entrada de la leyenda → toggle visibilidad de ese canal.
        """
        for legline in lined:
            legline.set_picker(8)

        def on_pick(event):
            legline = event.artist
            if legline not in lined:
                return
            origline = lined[legline]
            visible = not origline.get_visible()
            origline.set_visible(visible)
            legline.set_alpha(1.0 if visible else 0.25)
            canvas.draw_idle()

        canvas.mpl_connect("pick_event", on_pick)

    # ──────────────────────────────────────────────────────────────────
    # Vista Individual
    # ──────────────────────────────────────────────────────────────────
    def _setup_individual_tab(self, records, fmt):
        """Construye la pestaña Vista Individual después de cargar datos."""
        self._clear_tab(self._tab_indv)

        # Armar lista de canales disponibles con nombres legibles
        n_fast = 16 if fmt in (1, 2) else 10
        channel_opts = []
        for i in range(1, 33):
            group = "Rápido" if i <= n_fast else "Lento"
            channel_opts.append((f"ANA_{i}", f"ANA {i}  —  Analógico {group}"))
        for i in range(1, 14):
            label = IMU_LABELS.get(f"DSP_{i}", f"DSP {i}")
            units = IMU_UNITS.get(f"DSP_{i}", "")
            channel_opts.append((f"DSP_{i}", f"DSP {i}  —  {label} ({units})"))
        for i in range(1, 17):
            channel_opts.append((f"DIG_{i}", f"D{i}  —  Digital"))
        channel_opts.append(("HOR", "Hora GNSS"))
        channel_opts.append(("MIN", "Minuto GNSS"))
        channel_opts.append(("SEG", "Segundo GNSS"))
        channel_opts.append(("LAT", "Latitud GPS  [°]"))
        channel_opts.append(("LON", "Longitud GPS  [°]"))
        channel_opts.append(("ALT", "Altitud GPS  [m]"))

        self._indv_opts = {label: key for key, label in channel_opts}
        labels = [label for _, label in channel_opts]

        # ── Barra de control superior ──
        ctrl = tk.Frame(self._tab_indv, bg=BG_PANEL, height=48)
        ctrl.pack(fill="x", side="top")
        ctrl.pack_propagate(False)

        tk.Label(ctrl, text="Canal:", bg=BG_PANEL, fg=FG_MUTED,
                 font=("Segoe UI", 10)).pack(side="left", padx=(14, 6), pady=10)

        self._indv_combo = ttk.Combobox(ctrl, values=labels, state="readonly",
                                        font=("Segoe UI", 10), width=38)
        self._indv_combo.pack(side="left", pady=10)
        self._indv_combo.current(0)
        self._indv_combo.bind("<<ComboboxSelected>>", self._on_channel_selected)

        self._indv_stats = tk.Label(ctrl, text="", bg=BG_PANEL, fg=FG_MUTED,
                                    font=("Segoe UI", 9))
        self._indv_stats.pack(side="left", padx=20)

        # ── Área del gráfico ──
        self._indv_plot_frame = tk.Frame(self._tab_indv, bg=BG_DARK)
        self._indv_plot_frame.pack(fill="both", expand=True)

        # Dibujar primer canal
        self._draw_individual(labels[0], records)

    def _on_channel_selected(self, _event=None):
        if not self._records:
            return
        label = self._indv_combo.get()
        self._draw_individual(label, self._records)

    def _draw_individual(self, label: str, records):
        """Redibuja el gráfico de un solo canal en la Vista Individual."""
        key = self._indv_opts.get(label)
        if not key:
            return

        # Limpiar frame del gráfico anterior
        for w in self._indv_plot_frame.winfo_children():
            w.destroy()

        arrs = self._arrays
        # Vista individual: Datos reales sin decimar (resolución nativa a 75 Hz para zoom analítico)
        scale, time_label, _ = self._get_time_scale_and_label()
        t   = arrs["tiempo_s"] * scale
        raw_val = arrs.get(key, np.array([]))
        val = self._apply_cal(raw_val, key)

        # Calcular stats
        valid = val[~np.isnan(val)]
        if len(valid):
            stats_txt = (f"  mín: {valid.min():.4g}   "
                         f"máx: {valid.max():.4g}   "
                         f"media: {valid.mean():.4g}   "
                         f"σ: {valid.std():.4g}")
            self._indv_stats.config(text=stats_txt, fg=ACCENT)
        else:
            self._indv_stats.config(text="Sin datos", fg=YELLOW)

        # Color según tipo de canal
        if key.startswith("ANA"):
            color = ACCENT
        elif key.startswith("DSP"):
            color = GREEN
        elif key.startswith("DIG"):
            color = ORANGE
        else:
            color = YELLOW

        ylabel = self._channel_unit(key)
        if key.startswith("DIG") and not self._cal_mode.get():
            ylabel = "Estado (0/1)"

        # Crear figura grande
        fig = Figure(figsize=(10, 5), facecolor=BG_DARK, tight_layout=True)
        ax  = fig.add_subplot(1, 1, 1, facecolor=BG_PANEL)

        freq_mode = self._freq_mode.get() and not key.startswith("DIG")
        if freq_mode:
            fx, fy = magnitude_spectrum(val, self._series_fs())
            if len(fx):
                ax.plot(fx, fy, color=color, lw=1.2)
                ax.fill_between(fx, fy, alpha=0.08, color=color)
                peak_i = int(np.argmax(fy))
                ax.annotate(f"{fx[peak_i]:.2f} Hz",
                            xy=(fx[peak_i], fy[peak_i]),
                            xytext=(8, 8), textcoords="offset points",
                            color=FG_MUTED, fontsize=8)
            xlabel = "Frecuencia (Hz)"
            ylabel = f"|Y(f)|  [{ylabel}]" if ylabel else "|Y(f)|"
        elif key.startswith("DIG"):
            ax.fill_between(t, val, step="post", color=color, alpha=0.75)
            ax.plot(t, val, drawstyle="steps-post", color=color, lw=1.2)
            ax.set_ylim(-0.1, 1.3)
            ax.set_yticks([0, 1])
            ax.set_yticklabels(["0  (LOW)", "1  (HIGH)"], color=FG_MUTED, fontsize=9)
            xlabel = time_label
        elif key in ("SEG", "MIN", "HOR"):
            ax.plot(t, val, drawstyle="steps-post", color=color, lw=1.2)
            ax.fill_between(t, val, step="post", alpha=0.08, color=color)
            xlabel = time_label
        else:
            ax.plot(t, val, color=color, lw=1.2)
            ax.fill_between(t, val, alpha=0.08, color=color)
            xlabel = time_label

        ax.set_title(label, color=FG_TEXT, fontsize=12, pad=10)
        ax.set_xlabel(xlabel, color=FG_MUTED, fontsize=10)
        ax.set_ylabel(ylabel, color=FG_MUTED, fontsize=10)
        _style_axes(ax)

        # Anotaciones de mín/máx (solo dominio del tiempo)
        if (not freq_mode) and len(valid) and not key.startswith("DIG"):
            idx_max = int(np.nanargmax(val))
            idx_min = int(np.nanargmin(val))
            for idx, txt, va in [(idx_max, f"máx {valid.max():.4g}", "bottom"),
                                  (idx_min, f"mín {valid.min():.4g}", "top")]:
                ax.annotate(txt, xy=(t[idx], val[idx]),
                            xytext=(0, 12 if va == "bottom" else -12),
                            textcoords="offset points",
                            color=FG_MUTED, fontsize=8, ha="center", va=va,
                            arrowprops=dict(arrowstyle="-", color=FG_MUTED, lw=0.8))

        self._fig_indv = fig
        self._embed_figure(fig, self._indv_plot_frame)

    # ──────────────────────────────────────────────────────────────────
    # 📡  Monitor RS-232 en tiempo real  (CMD 0x0001 @ 2 Hz)
    # ──────────────────────────────────────────────────────────────────
    def _build_monitor_tab(self):
        tab = self._tab_mon

        self._mon_running = False
        self._mon_serial  = None
        self._mon_io_lock = threading.Lock()
        self._mon_values: dict = {}    # {channel_idx: int16}

        # ── Barra de control ──
        ctrl = tk.Frame(tab, bg=BG_PANEL)
        ctrl.pack(fill="x", side="top")

        row = tk.Frame(ctrl, bg=BG_PANEL)
        row.pack(fill="x", padx=12, pady=10)

        tk.Label(row, text="Puerto COM:", bg=BG_PANEL, fg=FG_MUTED,
                 font=("Segoe UI", 9)).pack(side="left", padx=(0, 4))

        self._mon_port = tk.StringVar(value="COM1")
        self._mon_port_combo = ttk.Combobox(row, textvariable=self._mon_port,
                                            values=["COM1"], font=("Segoe UI", 9),
                                            width=10)
        self._mon_port_combo.pack(side="left", padx=(0, 6))

        ttk.Button(row, text="🔄", width=3,
                   command=self._mon_refresh_ports).pack(side="left", padx=(0, 12))

        tk.Label(row, text="Baud:", bg=BG_PANEL, fg=FG_MUTED,
                 font=("Segoe UI", 9)).pack(side="left", padx=(0, 4))
        self._mon_baud = tk.StringVar(value="115200")
        ttk.Combobox(row, textvariable=self._mon_baud,
                     values=["9600", "19200", "38400", "57600", "115200"],
                     state="readonly", font=("Segoe UI", 9),
                     width=8).pack(side="left", padx=(0, 14))

        self._mon_btn = ttk.Button(row, text="▶  Conectar",
                                   style="Accent.TButton",
                                   command=self._mon_toggle)
        self._mon_btn.pack(side="left", padx=(0, 14))

        self._mon_status = tk.Label(row, text="Desconectado", bg=BG_PANEL,
                                    fg=FG_MUTED, font=("Segoe UI", 9))
        self._mon_status.pack(side="left")

        tk.Label(row, text="Actualización: 2 Hz", bg=BG_PANEL, fg=FG_MUTED,
                 font=("Segoe UI", 8, "italic")).pack(side="right")

        # ── Tabla de canales ──
        tree_frame = tk.Frame(tab, bg=BG_DARK)
        tree_frame.pack(fill="both", expand=True, padx=12, pady=10)

        cols = ("canal", "nombre", "raw", "cal", "unidad")
        self._mon_tree = ttk.Treeview(tree_frame, columns=cols, show="headings",
                                      selectmode="browse")
        self._mon_tree.heading("canal",  text="Canal")
        self._mon_tree.heading("nombre", text="Nombre")
        self._mon_tree.heading("raw",    text="Valor crudo (LSB)")
        self._mon_tree.heading("cal",    text="Valor calibrado")
        self._mon_tree.heading("unidad", text="Unidad")
        self._mon_tree.column("canal",  width=80,  anchor="center")
        self._mon_tree.column("nombre", width=200, anchor="w")
        self._mon_tree.column("raw",    width=130, anchor="e")
        self._mon_tree.column("cal",    width=130, anchor="e")
        self._mon_tree.column("unidad", width=80,  anchor="center")

        msb = ttk.Scrollbar(tree_frame, orient="vertical",
                            command=self._mon_tree.yview)
        self._mon_tree.configure(yscrollcommand=msb.set)
        self._mon_tree.pack(side="left", fill="both", expand=True)
        msb.pack(side="right", fill="y")

        # Poblar filas: ANA, DIG, IMU, tiempo GNSS y GPS
        self._mon_rows = []   # [(iid, channel_idx, param_key), ...]
        for i in range(32):
            key = f"ANA_{i+1}"
            iid = self._mon_tree.insert("", "end", values=(key, "", "—", "—", "LSB"))
            self._mon_rows.append((iid, i, key))
        for i in range(16):
            key = f"DIG_{i+1}"
            iid = self._mon_tree.insert("", "end", values=(key, "", "—", "—", "0/1"))
            self._mon_rows.append((iid, 32 + i, key))
        for i in range(13):
            key = f"DSP_{i+1}"
            name = IMU_LABELS.get(key, key)
            unit = IMU_UNITS.get(key, "")
            iid = self._mon_tree.insert("", "end",
                                        values=(key, name, "—", "—", unit))
            self._mon_rows.append((iid, CMD_CH_DSP_1 + i, key))
        for i, key in enumerate(TIME_COLS):
            name = TIME_LABELS.get(key, key)
            iid = self._mon_tree.insert("", "end",
                                        values=(key, name, "—", "—", ""))
            self._mon_rows.append((iid, CMD_CH_DIA + i, key))
        for key, name, unit, ch in (
            ("LAT", "Latitud GPS", "°", CMD_CH_LAT_H),
            ("LON", "Longitud GPS", "°", CMD_CH_LON_H),
            ("ALT", "Altitud GPS", "m", CMD_CH_ALT),
        ):
            iid = self._mon_tree.insert("", "end",
                                        values=(key, name, "—", "—", unit))
            self._mon_rows.append((iid, ch, key))

        # Doble clic en una fila → editar sensor/nombre/unidad del canal
        self._mon_tree.bind("<Double-1>", self._mon_edit_row)

        tk.Label(tab, text="💡  Doble clic en un canal para asignarle sensor, nombre y unidad",
                 bg=BG_DARK, fg=FG_MUTED, font=("Segoe UI", 8, "italic")).pack(
                     anchor="w", padx=14, pady=(0, 8))

        self._mon_refresh_ports()
        self._mon_apply_names()

    def _mon_edit_row(self, _event=None):
        sel = self._mon_tree.selection()
        if not sel:
            return
        iid = sel[0]
        key = next((k for (i, _, k) in self._mon_rows if i == iid), None)
        if key:
            self._edit_channel_info(key, on_done=lambda _k: self._mon_apply_names())

    def _mon_apply_names(self):
        """Vuelca nombre y unidad de la grilla a las filas del monitor (sin tocar valores)."""
        for iid, _ch, key in self._mon_rows:
            c    = self._grid_cfg.get(key, {})
            vals = list(self._mon_tree.item(iid, "values"))
            vals[1] = (c.get("name", "") or IMU_LABELS.get(key, "") or
                       TIME_LABELS.get(key, "") or
                       {"LAT": "Latitud GPS", "LON": "Longitud GPS",
                        "ALT": "Altitud GPS"}.get(key, vals[1] if len(vals) > 1 else ""))
            unit = (c.get("unit", "") or
                    self._cal_data.get(key, {}).get("unit", ""))
            if not unit:
                if key.startswith("DIG"):
                    unit = "0/1"
                elif key in IMU_UNITS:
                    unit = IMU_UNITS[key]
                elif key in ("LAT", "LON"):
                    unit = "°"
                elif key == "ALT":
                    unit = "m"
                elif key in TIME_COLS:
                    unit = ""
                else:
                    unit = "LSB"
            vals[4] = unit
            self._mon_tree.item(iid, values=vals)

    def _mon_refresh_ports(self):
        ports = ["COM1", "COM2", "COM3", "COM4", "COM5", "COM6", "COM7", "COM8"]
        try:
            import serial.tools.list_ports
            detected = [p.device for p in serial.tools.list_ports.comports()]
            if detected:
                ports = detected
                self._mon_port.set(detected[0])
        except ImportError:
            pass
        self._mon_port_combo.config(values=ports)

    def _mon_toggle(self):
        if self._mon_running:
            self._mon_stop()
        else:
            self._mon_start()

    def _mon_start(self):
        try:
            import serial
        except ImportError:
            messagebox.showerror(
                "pyserial no instalado",
                "Instalá pyserial con:\n  pip install pyserial\n\n"
                "Luego reiniciá la aplicación.")
            return

        port = self._mon_port.get().strip()
        baud = int(self._mon_baud.get())
        try:
            # Abrir SIN afirmar DTR/RTS: en el DevKit del ESP32-S3 esas líneas
            # están cableadas a EN/IO0. Si se afirman, la placa queda en reset
            # (LED apagado, sin datos). Las dejamos en bajo para que corra normal.
            ser = serial.Serial()
            ser.port     = port
            ser.baudrate = baud
            ser.timeout  = 0.2
            ser.dtr      = False
            ser.rts      = False
            ser.open()
            self._mon_serial = ser
        except serial.SerialException as exc:
            messagebox.showerror("Error de conexión",
                                 f"No se pudo abrir {port}:\n{exc}")
            return

        self._mon_running = True
        self._mon_btn.config(text="⏹  Desconectar")
        self._mon_status.config(text=f"Conectado a {port} @ {baud}", fg=GREEN)

        threading.Thread(target=self._mon_poll_loop, daemon=True).start()

    def _mon_stop(self):
        self._mon_running = False
        self._mon_btn.config(text="▶  Conectar")
        self._mon_status.config(text="Desconectado", fg=FG_MUTED)
        if self._mon_serial:
            try:
                self._mon_serial.close()
            except Exception:
                pass
            self._mon_serial = None

    def _mon_query_channel(self, ch: int):
        """
        Envía CMD_READ_PARAM (0x0001) para un canal y devuelve el valor int16.

        Lee únicamente los bytes disponibles (in_waiting) y retorna apenas
        encuentra el frame de respuesta, sin esperar timeouts innecesarios.
        Busca el header 0x55 0xAA (little-endian de 0xAA55) dentro del flujo
        para resincronizar por si quedó algún byte suelto.
        """
        import time

        msg = struct.pack("<HHH", 0xAA55, 0x0001, ch)
        self._mon_serial.reset_input_buffer()
        self._mon_serial.write(msg)

        deadline = time.monotonic() + 0.1
        buf = bytearray()
        while time.monotonic() < deadline:
            n = self._mon_serial.in_waiting
            if not n:
                time.sleep(0.001)
                continue
            buf.extend(self._mon_serial.read(n))

            search = 0
            while True:
                pos = buf.find(b"\x55\xAA", search)
                if pos == -1 or pos + 6 > len(buf):
                    break
                hdr, cmd, raw = struct.unpack("<HHh", bytes(buf[pos:pos + 6]))
                if hdr == 0xAA55 and cmd == 0x0001:
                    return raw
                search = pos + 2

            # Conservar la cola por si quedó un header partido.
            if len(buf) > 512:
                del buf[:-8]
        return None

    def _mon_poll_loop(self):
        """Hilo de sondeo: lee todos los canales con CMD 0x0001 y repite cada 500 ms (2 Hz)."""
        import time

        while self._mon_running and self._mon_serial:
            t_start = time.monotonic()
            values  = {}
            error   = None
            if not self._mon_io_lock.acquire(timeout=0.05):
                time.sleep(0.02)
                continue
            try:
                query_chs = list(range(CMD_CH_COUNT))
                for ch in query_chs:
                    if not self._mon_running:
                        return
                    try:
                        raw = self._mon_query_channel(ch)
                        if raw is not None:
                            values[ch] = raw
                    except Exception as exc:
                        error = str(exc)
                        break
            finally:
                self._mon_io_lock.release()

            self.after(0, lambda v=values, e=error: self._mon_update_table(v, e))

            if error:
                self.after(0, self._mon_stop)
                return

            # Mantener el período de 500 ms (2 Hz)
            elapsed = time.monotonic() - t_start
            remaining = 0.5 - elapsed
            if remaining > 0:
                time.sleep(remaining)

    def _mon_update_table(self, values: dict, error: str | None):
        if error:
            self._mon_status.config(text=f"Error: {error}", fg=RED)
            return

        n_ok = len(values)
        for iid, ch, key in self._mon_rows:
            raw = values.get(ch)
            if raw is None:
                continue

            grid_c = self._grid_cfg.get(key, {})
            cal_c  = self._cal_data.get(key, {})

            # Nombre: grilla → calibración → etiqueta conocida
            name = (grid_c.get("name", "") or cal_c.get("name", "") or
                    IMU_LABELS.get(key, "") or TIME_LABELS.get(key, "") or
                    {"LAT": "Latitud GPS", "LON": "Longitud GPS",
                     "ALT": "Altitud GPS"}.get(key, ""))

            # Unidad: grilla → calibración → por defecto
            unit_str = (grid_c.get("unit", "") or cal_c.get("unit", "") or
                        IMU_UNITS.get(key, "") or
                        ("0/1" if key.startswith("DIG") else
                         ("°" if key in ("LAT", "LON") else
                          ("m" if key == "ALT" else
                           ("" if key in TIME_COLS else "LSB")))))

            if key == "LAT":
                val = _reconstruct_latlon(values.get(CMD_CH_LAT_H),
                                          values.get(CMD_CH_LAT_L))
                raw_str = f"{val:.6f}" if val is not None else "—"
                cal_str = raw_str
                unit_str = unit_str or "°"
            elif key == "LON":
                val = _reconstruct_latlon(values.get(CMD_CH_LON_H),
                                          values.get(CMD_CH_LON_L))
                raw_str = f"{val:.6f}" if val is not None else "—"
                cal_str = raw_str
                unit_str = unit_str or "°"
            elif key == "ALT":
                raw_str = str(raw)
                cal_str = f"{raw * 0.3048:.1f}"
                unit_str = unit_str or "m"
            elif key.startswith("DIG"):
                raw_str = "1  (HIGH)" if raw else "0  (LOW)"
                cal_str = "—"
            elif cal_c.get("coeffs"):
                raw_str = str(raw)
                cal_str = f"{float(np.polyval(cal_c['coeffs'], raw)):.4g}"
            elif key in IMU_SCALES:
                raw_str = str(raw)
                cal_str = f"{raw / IMU_SCALES[key]:.4g}"
                unit_str = unit_str or IMU_UNITS.get(key, "")
            else:
                raw_str = str(raw)
                cal_str = "—"

            self._mon_tree.item(iid, values=(key, name, raw_str, cal_str, unit_str))

        import datetime
        ts = datetime.datetime.now().strftime("%H:%M:%S")
        self._mon_status.config(
            text=f"Conectado — última lectura {ts}  ({n_ok}/{len(self._mon_rows)} canales)",
            fg=GREEN)

    # ──────────────────────────────────────────────────────────────────
    # Formatear SD via RS-232  (CMD 0x0004)
    # ──────────────────────────────────────────────────────────────────
    def _format_sd_dialog(self):
        dlg = tk.Toplevel(self)
        dlg.title("Formatear MicroSD")
        dlg.configure(bg=BG_DARK)
        dlg.geometry("420x320")
        dlg.resizable(False, False)
        dlg.grab_set()

        # Centrar
        dlg.update_idletasks()
        x = self.winfo_x() + (self.winfo_width()  - 420) // 2
        y = self.winfo_y() + (self.winfo_height() - 320) // 2
        dlg.geometry(f"+{x}+{y}")

        tk.Label(dlg, text="⚠  FORMATEAR MICROSD", bg=BG_DARK, fg=RED,
                 font=("Segoe UI", 13, "bold")).pack(pady=(18, 4))
        tk.Label(dlg,
                 text="Esta operación borrará TODOS los datos\n"
                      "almacenados en la MicroSD del MAD.\n"
                      "La operación NO se puede deshacer.",
                 bg=BG_DARK, fg=FG_MUTED, font=("Segoe UI", 9),
                 justify="center").pack(pady=(0, 14))

        tk.Frame(dlg, bg=BG_PANEL, height=1).pack(fill="x", padx=20)

        # Puerto COM
        frm = tk.Frame(dlg, bg=BG_DARK)
        frm.pack(fill="x", padx=20, pady=12)

        tk.Label(frm, text="Puerto COM:", bg=BG_DARK, fg=FG_MUTED,
                 font=("Segoe UI", 9), width=12, anchor="e").grid(
                     row=0, column=0, padx=(0, 8), pady=4, sticky="e")
        port_var = tk.StringVar(value="COM1")

        # Intentar listar puertos con pyserial
        port_list = ["COM1","COM2","COM3","COM4","COM5","COM6","COM7","COM8"]
        try:
            import serial.tools.list_ports
            detected = [p.device for p in serial.tools.list_ports.comports()]
            if detected:
                port_list = detected
                port_var.set(detected[0])
        except ImportError:
            pass

        ttk.Combobox(frm, textvariable=port_var, values=port_list,
                     font=("Segoe UI", 9), width=14).grid(row=0, column=1, sticky="w")

        tk.Label(frm, text="Baud rate:", bg=BG_DARK, fg=FG_MUTED,
                 font=("Segoe UI", 9), width=12, anchor="e").grid(
                     row=1, column=0, padx=(0, 8), pady=4, sticky="e")
        baud_var = tk.StringVar(value="115200")
        ttk.Combobox(frm, textvariable=baud_var,
                     values=["9600","19200","38400","57600","115200"],
                     state="readonly", font=("Segoe UI", 9), width=14).grid(
                         row=1, column=1, sticky="w")

        status_lbl = tk.Label(dlg, text="", bg=BG_DARK, fg=FG_MUTED,
                              font=("Segoe UI", 8))
        status_lbl.pack(pady=(0, 8))

        def _send():
            try:
                import serial
            except ImportError:
                messagebox.showerror(
                    "pyserial no instalado",
                    "Instalá pyserial con:\n  pip install pyserial\n\n"
                    "Luego reiniciá la aplicación.",
                    parent=dlg)
                return

            if not messagebox.askyesno(
                    "Confirmar formateo",
                    "¿Estás SEGURO de que querés formatear la MicroSD?\n\n"
                    "Se borrarán TODOS los vuelos almacenados.",
                    icon="warning", parent=dlg):
                return

            import time as _time
            port = port_var.get().strip()
            baud = int(baud_var.get())

            # Si el monitor RS-232 está usando este mismo puerto, hay que
            # liberarlo: Windows no permite dos conexiones al mismo COM.
            resume_monitor = False
            if getattr(self, "_mon_running", False) and self._mon_serial is not None:
                try:
                    mon_port = self._mon_serial.port
                except Exception:
                    mon_port = None
                if mon_port and mon_port.upper() == port.upper():
                    resume_monitor = True
                    status_lbl.config(text="Pausando monitor para liberar el puerto…",
                                      fg=YELLOW)
                    dlg.update_idletasks()
                    self._mon_stop()
                    _time.sleep(0.4)   # dar tiempo a que el hilo cierre el COM

            # El formateo de la SD es bloqueante en el firmware y puede tardar
            # varios minutos según el tamaño de la tarjeta. Lo hacemos en un
            # hilo para no congelar la ventana, con un contador en vivo.
            FORMAT_TIMEOUT = 300.0   # 5 min de margen
            status_lbl.config(text="⏳  Formateando… puede tardar varios minutos. Esperá.",
                              fg=YELLOW)
            fmt_btn.config(state="disabled")
            dlg.update_idletasks()

            def _set_status(text, color):
                if dlg.winfo_exists():
                    status_lbl.config(text=text, fg=color)

            def _worker():
                import struct as _struct
                import serial as _serial
                res = {"status": None, "color": FG_MUTED, "popup": None, "err": None}
                try:
                    # Protocolo: [header 0xAA55][cmd 0x0004][key 0x55AA] little-endian
                    msg = _struct.pack("<HHH", 0xAA55, 0x0004, 0x55AA)

                    ser = _serial.Serial()
                    ser.port     = port
                    ser.baudrate = baud
                    ser.timeout  = 0.5
                    ser.dtr      = False
                    ser.rts      = False
                    ser.open()

                    try:
                        _time.sleep(2.5)
                        ser.reset_input_buffer()
                        ser.write(msg)
                        # El ACK llega recién cuando termina de formatear.
                        t0 = _time.monotonic()
                        deadline = t0 + FORMAT_TIMEOUT
                        last_tick = -1
                        buf = bytearray()
                        ack = None
                        while _time.monotonic() < deadline:
                            chunk = ser.read(64)
                            # Contador en vivo (cada segundo) para que se vea vivo.
                            elapsed = int(_time.monotonic() - t0)
                            if elapsed != last_tick:
                                last_tick = elapsed
                                self.after(0, _set_status,
                                           f"⏳  Formateando… {elapsed}s "
                                           f"(máx {int(FORMAT_TIMEOUT)}s). Esperá.",
                                           YELLOW)
                            if chunk:
                                buf.extend(chunk)
                                pos = buf.find(b"\x55\xAA")
                                if pos != -1 and pos + 6 <= len(buf):
                                    _, cmd_r, data_r = _struct.unpack(
                                        "<HHh", bytes(buf[pos:pos + 6]))
                                    if cmd_r == 0x0004:
                                        ack = data_r
                                        break
                    finally:
                        ser.close()

                    if ack is None:
                        res["status"] = (f"⚠  Sin respuesta del MAD tras "
                                         f"{int(FORMAT_TIMEOUT)}s. "
                                         "Revisá los logs del firmware (CMD4).")
                        res["color"] = YELLOW
                    elif ack == 0x0001:   # CMD_ACK_OK
                        res["status"] = "✓  Formateo completado exitosamente."
                        res["color"] = GREEN
                        res["popup"] = ("Listo", "MicroSD formateada correctamente.")
                    else:
                        res["status"] = f"⚠  MAD respondió con error: 0x{ack & 0xFFFF:04X}"
                        res["color"] = RED

                except _serial.SerialException as exc:
                    txt = str(exc)
                    if ("PermissionError" in txt or "Acceso denegado" in txt
                            or "denied" in txt.lower()):
                        res["status"] = (f"⚠  {port} ocupado. Cerrá el monitor RS-232, "
                                         "PuTTY u otro programa que use ese puerto.")
                    else:
                        res["status"] = f"Error serial: {exc}"
                    res["color"] = RED
                except Exception as exc:
                    res["err"] = str(exc)

                def _finish():
                    if dlg.winfo_exists():
                        fmt_btn.config(state="normal")
                        if res["err"]:
                            messagebox.showerror("Error", res["err"], parent=dlg)
                        else:
                            status_lbl.config(text=res["status"], fg=res["color"])
                            if res["popup"]:
                                messagebox.showinfo(res["popup"][0], res["popup"][1],
                                                    parent=dlg)
                    if resume_monitor:
                        self.after(300, self._mon_start)

                self.after(0, _finish)

            threading.Thread(target=_worker, daemon=True).start()

        btn_row = tk.Frame(dlg, bg=BG_DARK)
        btn_row.pack(pady=8)

        # Botón rojo de confirmación
        fmt_btn = tk.Button(btn_row, text="🗑  FORMATEAR",
                  bg=RED, fg="#1e1e2e", font=("Segoe UI", 10, "bold"),
                  relief="flat", padx=12, pady=6,
                  command=_send)
        fmt_btn.pack(side="left", padx=6)
        ttk.Button(btn_row, text="Cancelar",
                   command=dlg.destroy).pack(side="left", padx=6)

    # ──────────────────────────────────────────────────────────────────
    # Exportar gráfico + reporte PDF
    # ──────────────────────────────────────────────────────────────────
    def _current_figure(self):
        """Devuelve la figura matplotlib de la pestaña actualmente visible."""
        tab_map = {
            str(self._tab_ana):  getattr(self, "_fig_ana",  None),
            str(self._tab_imu):  getattr(self, "_fig_imu",  None),
            str(self._tab_dig):  getattr(self, "_fig_dig",  None),
            str(self._tab_time): getattr(self, "_fig_time", None),
            str(self._tab_gps):  getattr(self, "_fig_gps",  None),
            str(self._tab_indv): getattr(self, "_fig_indv", None),
            str(self._tab_psd):  getattr(self, "_fig_psd",  None),
        }
        current = str(self._notebook.nametowidget(self._notebook.select()))
        return tab_map.get(current)

    def _export_current_plot(self):
        fig = self._current_figure()
        if fig is None:
            messagebox.showwarning("Sin gráfico",
                                   "Convertí un archivo primero o seleccioná una pestaña con gráfico.")
            return
        path = filedialog.asksaveasfilename(
            title="Exportar gráfico",
            defaultextension=".png",
            filetypes=[("PNG", "*.png"), ("PDF", "*.pdf"),
                       ("SVG", "*.svg"), ("Todos", "*.*")])
        if not path:
            return
        try:
            fig.savefig(path, dpi=150, bbox_inches="tight",
                        facecolor=fig.get_facecolor())
            self._set_status(f"Gráfico exportado: {Path(path).name}", 100)
        except Exception as exc:
            messagebox.showerror("Error al exportar", str(exc))

    def _generate_report(self):
        if not self._records:
            messagebox.showwarning("Sin datos", "Convertí un archivo .bin primero.")
            return

        path = filedialog.asksaveasfilename(
            title="Guardar reporte PDF",
            defaultextension=".pdf",
            filetypes=[("PDF", "*.pdf")])
        if not path:
            return

        self._set_status("Generando reporte PDF…", 20)
        self.update_idletasks()

        try:
            from matplotlib.backends.backend_pdf import PdfPages
            import datetime

            records    = self._records
            arrs       = self._arrays
            fmt        = self._cev_format
            cps        = {1: 75, 2: 75, 3: 10}[fmt]
            n_cycles   = len(records)
            dur_s      = n_cycles / cps
            n_fast     = 16 if fmt in (1, 2) else 10
            bin_path   = self._bin_path.get()
            cal_active = self._cal_mode.get() and bool(self._cal_data)
            step       = max(1, n_cycles // MAX_PLOT_POINTS)
            scale, time_label, _ = self._get_time_scale_and_label()
            t          = _decimate(arrs["tiempo_s"], step) * scale
            now_str    = datetime.datetime.now().strftime("%d/%m/%Y  %H:%M")
            A4         = (8.27, 11.69)

            fmt_names = {
                1: "Formato 1  (40×17 words, 75 ciclos/s)",
                2: "Formato 2  (40×11 words, 75 ciclos/s)",
                3: "Formato 3  (10×6 words,  10 ciclos/s)",
            }

            def _save(fig, pdf_obj):
                pdf_obj.savefig(fig, facecolor=BG_DARK, bbox_inches="tight")
                matplotlib.pyplot.close(fig)

            def _hdr(fig, title, subtitle=""):
                """Banda de cabecera común en todas las páginas."""
                fig.text(0.06, 0.965, title, color=ACCENT,
                         fontsize=13, fontweight="bold", va="top")
                if subtitle:
                    fig.text(0.06, 0.945, subtitle, color=FG_MUTED,
                             fontsize=8, va="top")
                fig.text(0.94, 0.965,
                         f"FDR Converter · CEV-FAA  |  {now_str}",
                         color=FG_MUTED, fontsize=7, ha="right", va="top")

            # ── Colectar estadísticas de todos los canales ─────────────
            def _chan_stats(key):
                arr   = arrs.get(key, np.array([]))
                valid = arr[~np.isnan(arr)]
                if len(valid) == 0:
                    return None
                if cal_active:
                    valid = self._apply_cal(valid, key)
                return {
                    "min":  float(valid.min()),
                    "max":  float(valid.max()),
                    "mean": float(valid.mean()),
                    "std":  float(valid.std()),
                    "unit": (self._cal_data.get(key, {}).get("unit", "")
                             if cal_active else ("" if not key.startswith("DSP")
                             else IMU_UNITS.get(key, ""))),
                    "name": (self._cal_data.get(key, {}).get("name", "")
                             or self._grid_cfg.get(key, {}).get("name", "")),
                }

            all_stats = {}
            for key in ANA_COLS + DSP_COLS:
                s = _chan_stats(key)
                if s:
                    all_stats[key] = s

            with PdfPages(path) as pdf:

                # ════════════════════════════════════════════════════
                # 1.  PORTADA
                # ════════════════════════════════════════════════════
                fig = Figure(figsize=A4, facecolor=BG_DARK)
                ax  = fig.add_axes([0, 0, 1, 1], facecolor=BG_DARK)
                ax.axis("off")
                ax.plot([0.08, 0.92], [0.91, 0.91], color=ACCENT, lw=5,
                        transform=ax.transAxes)
                ax.text(0.5, 0.84, "REPORTE DE VUELO", ha="center",
                        color=ACCENT, fontsize=26, fontweight="bold",
                        transform=ax.transAxes)
                ax.text(0.5, 0.79, "Flight Data Recorder  —  CEV-FAA / IUA",
                        ha="center", color=FG_MUTED, fontsize=13,
                        transform=ax.transAxes)
                ax.plot([0.08, 0.92], [0.76, 0.76], color=FG_MUTED, lw=0.5,
                        transform=ax.transAxes)
                info_lines = [
                    ("Archivo",        Path(bin_path).name if bin_path else "—"),
                    ("Formato CEV",    fmt_names[fmt]),
                    ("Ciclos totales", f"{n_cycles:,}"),
                    ("Duración",       f"{dur_s:.1f} s  ({dur_s/60:.1f} min)"),
                    ("Canales con datos", str(len(all_stats))),
                    ("Calibración",    "Aplicada" if cal_active else "Sin calibración (LSB crudo)"),
                    ("Fecha de reporte", now_str),
                ]
                y0 = 0.71
                for lbl, val in info_lines:
                    ax.text(0.14, y0, lbl + ":", color=FG_MUTED, fontsize=11,
                            transform=ax.transAxes, va="center")
                    ax.text(0.42, y0, val, color=FG_TEXT, fontsize=11,
                            transform=ax.transAxes, va="center")
                    y0 -= 0.057
                ax.plot([0.08, 0.92], [0.26, 0.26], color=FG_MUTED, lw=0.5,
                        transform=ax.transAxes)
                # Mini índice
                index_items = [
                    "1. Portada e información general",
                    "2. Tabla de estadísticas por canal",
                    "3. Configuración de grilla" + (" (incluida)" if self._grid_cfg else " (sin datos)"),
                    "4. Resumen de calibración" + (" (incluida)" if self._cal_data else " (sin datos)"),
                    "5. Señales Analógicas — canales rápidos",
                    "6. Señales Analógicas — canales lentos",
                    "7–9. IMU (Aceleración, Giróscopo, Actitud)",
                    "10. Señales Discretas",
                    "11. Tiempo GNSS",
                    "12–14. PSD Acelerómetros (G²/Hz)",
                    "15+. Vista individual por canal",
                ]
                ax.text(0.14, 0.24, "Contenido:", color=FG_MUTED, fontsize=9,
                        fontweight="bold", transform=ax.transAxes, va="top")
                y1 = 0.21
                for item in index_items:
                    ax.text(0.16, y1, "·  " + item, color=FG_MUTED, fontsize=8,
                            transform=ax.transAxes, va="top")
                    y1 -= 0.018
                ax.text(0.5, 0.04, "Generado por FDR Converter  ·  CEV-FAA",
                        ha="center", color=FG_MUTED, fontsize=9,
                        transform=ax.transAxes)
                _save(fig, pdf)

                # ════════════════════════════════════════════════════
                # 2.  TABLA DE ESTADÍSTICAS
                # ════════════════════════════════════════════════════
                def _stats_page(keys, title):
                    rows = []
                    for k in keys:
                        s = all_stats.get(k)
                        if not s:
                            continue
                        name = s["name"] or k
                        unit = s["unit"] or "LSB"
                        rows.append([k, name, unit,
                                     f"{s['min']:.4g}", f"{s['max']:.4g}",
                                     f"{s['mean']:.4g}", f"{s['std']:.4g}"])
                    if not rows:
                        return
                    fig = Figure(figsize=A4, facecolor=BG_DARK)
                    _hdr(fig, title, "Estadísticas sobre la duración completa del vuelo")
                    ax = fig.add_axes([0.04, 0.04, 0.92, 0.88], facecolor=BG_DARK)
                    ax.axis("off")
                    col_hdrs = ["Canal", "Nombre", "Unidad", "Mínimo", "Máximo",
                                "Media", "Desv. estándar"]
                    tbl = ax.table(cellText=rows, colLabels=col_hdrs,
                                   loc="upper center", cellLoc="center")
                    tbl.auto_set_font_size(False)
                    tbl.set_fontsize(8)
                    tbl.scale(1, 1.4)
                    for (r, c), cell in tbl.get_celld().items():
                        cell.set_facecolor(BG_CARD if r == 0 else
                                           (BG_PANEL if r % 2 == 0 else BG_DARK))
                        cell.set_text_props(color=ACCENT if r == 0 else FG_TEXT)
                        cell.set_edgecolor(BG_PANEL)
                    _save(fig, pdf)

                _stats_page(ANA_COLS, "Estadísticas — Señales Analógicas")
                _stats_page(DSP_COLS, "Estadísticas — IMU / DSP")

                # ════════════════════════════════════════════════════
                # 3.  CONFIGURACIÓN DE GRILLA
                # ════════════════════════════════════════════════════
                if self._grid_cfg:
                    rows = []
                    for i in range(1, 33):
                        key = f"ANA_{i}"
                        c   = self._grid_cfg.get(key, {})
                        if not any(c.get(f) for f in ("name","sensor","rate","range_min","range_max")):
                            continue
                        tasa = "Rápido" if c.get("rate") == "fast" else (
                               "Lento"  if c.get("rate") == "slow"  else "—")
                        rows.append([key, c.get("name","—"), c.get("sensor","—"),
                                     tasa,
                                     str(c.get("range_min","—")),
                                     str(c.get("range_max","—"))])
                    if rows:
                        fig = Figure(figsize=A4, facecolor=BG_DARK)
                        _hdr(fig, "Configuración de Grilla de Muestreo",
                             f"Bit rate: {self._bitrate_var.get()}")
                        ax = fig.add_axes([0.04, 0.04, 0.92, 0.88], facecolor=BG_DARK)
                        ax.axis("off")
                        col_hdrs = ["Canal", "Nombre", "Sensor / Transductor",
                                    "Tasa", "Rng mín", "Rng máx"]
                        tbl = ax.table(cellText=rows, colLabels=col_hdrs,
                                       loc="upper center", cellLoc="center")
                        tbl.auto_set_font_size(False)
                        tbl.set_fontsize(8)
                        tbl.scale(1, 1.4)
                        for (r, c_idx), cell in tbl.get_celld().items():
                            cell.set_facecolor(BG_CARD if r == 0 else
                                               (BG_PANEL if r % 2 == 0 else BG_DARK))
                            cell.set_text_props(color=ACCENT if r == 0 else FG_TEXT)
                            cell.set_edgecolor(BG_PANEL)
                        _save(fig, pdf)

                # ════════════════════════════════════════════════════
                # 4.  RESUMEN DE CALIBRACIÓN
                # ════════════════════════════════════════════════════
                if self._cal_data:
                    rows = []
                    for key, c in sorted(self._cal_data.items()):
                        if not c.get("coeffs"):
                            continue
                        coef_str = ", ".join(f"{v:.6g}" for v in c["coeffs"])
                        terms = []
                        coeffs = c["coeffs"]
                        for i, coef in enumerate(coeffs):
                            exp = len(coeffs) - 1 - i
                            if exp == 0:
                                terms.append(f"{coef:+.4g}")
                            elif exp == 1:
                                terms.append(f"{coef:+.4g}·x")
                            else:
                                terms.append(f"{coef:+.4g}·x^{exp}")
                        formula = "y = " + " ".join(terms)
                        rows.append([key, c.get("name","—"), c.get("unit","—"),
                                     c.get("mode","—"), formula])
                    if rows:
                        fig = Figure(figsize=A4, facecolor=BG_DARK)
                        _hdr(fig, "Resumen de Calibración de Canales",
                             "Coeficientes calculados por mínimos cuadrados")
                        ax = fig.add_axes([0.04, 0.04, 0.92, 0.88], facecolor=BG_DARK)
                        ax.axis("off")
                        col_hdrs = ["Canal", "Nombre", "Unidad", "Modo", "Fórmula"]
                        tbl = ax.table(cellText=rows, colLabels=col_hdrs,
                                       loc="upper center", cellLoc="left")
                        tbl.auto_set_font_size(False)
                        tbl.set_fontsize(7.5)
                        tbl.scale(1, 1.5)
                        for (r, c_idx), cell in tbl.get_celld().items():
                            cell.set_facecolor(BG_CARD if r == 0 else
                                               (BG_PANEL if r % 2 == 0 else BG_DARK))
                            cell.set_text_props(color=ACCENT if r == 0 else FG_TEXT)
                            cell.set_edgecolor(BG_PANEL)
                        _save(fig, pdf)

                # ════════════════════════════════════════════════════
                # 5–6.  ANALÓGICAS (rápidas y lentas por separado)
                # ════════════════════════════════════════════════════
                palette_ana = matplotlib.pyplot.cm.tab20.colors

                def _ana_page(idx_from, idx_to, label):
                    keys_pg = [f"ANA_{i}" for i in range(idx_from, idx_to + 1)]
                    keys_pg = [k for k in keys_pg if all_stats.get(k)]
                    if not keys_pg:
                        return
                    fig = Figure(figsize=A4, facecolor=BG_DARK, tight_layout=True)
                    _hdr(fig, f"Señales Analógicas — {label}",
                         f"Canales ANA {idx_from}–{idx_to}  |  {len(keys_pg)} activos")
                    ax = fig.add_subplot(1, 1, 1, facecolor=BG_PANEL)
                    fig.subplots_adjust(top=0.89, bottom=0.07, left=0.09, right=0.97)
                    for ci, key in enumerate(keys_pg):
                        v  = self._apply_cal(_decimate(arrs.get(key, np.array([])), step), key)
                        ln = all_stats[key]["name"] or key
                        ax.plot(t, v, lw=0.7, label=ln,
                                color=palette_ana[ci % len(palette_ana)])
                    ylabel = "Unidad calibrada" if cal_active else "LSB"
                    ax.set_xlabel(time_label, color=FG_MUTED, fontsize=9)
                    ax.set_ylabel(ylabel, color=FG_MUTED, fontsize=9)
                    _style_axes(ax)
                    ax.legend(loc="upper right", fontsize=6, ncol=4,
                              facecolor=BG_CARD, edgecolor="none", labelcolor=FG_TEXT)
                    _save(fig, pdf)

                _ana_page(1, n_fast, "Canales Rápidos")
                _ana_page(n_fast + 1, 32, "Canales Lentos")

                # ════════════════════════════════════════════════════
                # 7–9.  IMU
                # ════════════════════════════════════════════════════
                imu_pages = [
                    ("IMU — Aceleración",   ["DSP_1","DSP_2","DSP_3"],  "m/s²"),
                    ("IMU — Giróscopo",     ["DSP_4","DSP_5","DSP_6"],  "°/s"),
                    ("IMU — Actitud y Mag", ["DSP_7","DSP_8","DSP_9",
                                             "DSP_10","DSP_11","DSP_12"], ""),
                ]
                pal_imu = [ACCENT, GREEN, ORANGE, RED, "#cba6f7", YELLOW]
                for pg_title, keys_pg, unit_pg in imu_pages:
                    keys_pg = [k for k in keys_pg if all_stats.get(k)]
                    if not keys_pg:
                        continue
                    fig = Figure(figsize=A4, facecolor=BG_DARK, tight_layout=True)
                    _hdr(fig, pg_title)
                    ax = fig.add_subplot(1, 1, 1, facecolor=BG_PANEL)
                    fig.subplots_adjust(top=0.89, bottom=0.07, left=0.09, right=0.97)
                    for ci, key in enumerate(keys_pg):
                        v = _decimate(arrs.get(key, np.array([])), step)
                        ax.plot(t, v, lw=0.9,
                                label=IMU_LABELS.get(key, key),
                                color=pal_imu[ci % len(pal_imu)])
                    ax.set_xlabel(time_label, color=FG_MUTED, fontsize=9)
                    ax.set_ylabel(unit_pg, color=FG_MUTED, fontsize=9)
                    _style_axes(ax)
                    ax.legend(loc="upper right", fontsize=8,
                              facecolor=BG_CARD, edgecolor="none", labelcolor=FG_TEXT)
                    _save(fig, pdf)

                # ════════════════════════════════════════════════════
                # 10.  DISCRETAS
                # ════════════════════════════════════════════════════
                fig = Figure(figsize=A4, facecolor=BG_DARK, tight_layout=True)
                _hdr(fig, "Señales Discretas", "16 canales digitales")
                fig.subplots_adjust(top=0.89, bottom=0.04, left=0.08, right=0.97,
                                    hspace=0.55, wspace=0.3)
                for i in range(1, 17):
                    ax = fig.add_subplot(4, 4, i, facecolor=BG_PANEL)
                    v  = _decimate(arrs.get(f"DIG_{i}", np.array([])), step)
                    if len(v):
                        ax.fill_between(t, v, step="post", color=ACCENT, alpha=0.7)
                    ax.set_ylim(-0.1, 1.2)
                    ax.set_yticks([0, 1])
                    ax.set_yticklabels(["0","1"], fontsize=5, color=FG_MUTED)
                    ax.set_title(f"D{i}", color=FG_TEXT, fontsize=7)
                    _style_axes(ax, tiny=True)
                _save(fig, pdf)

                # ════════════════════════════════════════════════════
                # 11.  TIEMPO GNSS
                # ════════════════════════════════════════════════════
                fig = Figure(figsize=A4, facecolor=BG_DARK, tight_layout=True)
                _hdr(fig, "Tiempo GNSS")
                fig.subplots_adjust(top=0.89, bottom=0.06, left=0.09, right=0.97)
                for idx_g, (key_g, ttl_g) in enumerate(
                        [("HOR","Hora"),("MIN","Minuto"),("SEG","Segundo")], 1):
                    ax = fig.add_subplot(3, 1, idx_g, facecolor=BG_PANEL)
                    v  = _decimate(arrs.get(key_g, np.array([])), step)
                    if len(v):
                        ax.plot(t, v, lw=0.9, color=YELLOW)
                    ax.set_title(ttl_g, color=FG_TEXT, fontsize=9)
                    ax.set_xlabel(time_label, color=FG_MUTED, fontsize=8)
                    _style_axes(ax)
                _save(fig, pdf)

                # ════════════════════════════════════════════════════
                # 11b. POSICIÓN GPS (LAT / LON / ALT)
                # ════════════════════════════════════════════════════
                lat_arr = _decimate(arrs.get("LAT", np.array([])), step)
                lon_arr = _decimate(arrs.get("LON", np.array([])), step)
                alt_arr = _decimate(arrs.get("ALT", np.array([])), step)
                if (len(lat_arr) > 0 and not np.all(np.isnan(lat_arr))):
                    fig = Figure(figsize=A4, facecolor=BG_DARK, tight_layout=True)
                    _hdr(fig, "Posición GPS  (LAT / LON / ALT)",
                         "Solo disponible en Formato 2 y Formato 3")
                    fig.subplots_adjust(top=0.88, bottom=0.06, left=0.1, right=0.96)
                    for idx_g, (arr_g, ttl_g, col_g, ylbl_g) in enumerate([
                            (lat_arr, "Latitud",  ACCENT, "°"),
                            (lon_arr, "Longitud", GREEN,  "°"),
                            (alt_arr, "Altitud",  ORANGE, "m"),
                    ], 1):
                        ax = fig.add_subplot(3, 1, idx_g, facecolor=BG_PANEL)
                        ax.plot(t, arr_g, lw=0.9, color=col_g)
                        ax.set_title(ttl_g, color=FG_TEXT, fontsize=9)
                        ax.set_xlabel(time_label, color=FG_MUTED, fontsize=8)
                        ax.set_ylabel(ylbl_g, color=FG_MUTED, fontsize=8)
                        _style_axes(ax)
                    _save(fig, pdf)

                # ════════════════════════════════════════════════════
                # 12–14.  PSD AUTO — ACELERÓMETROS
                # ════════════════════════════════════════════════════
                for acc_key, acc_lbl in [("DSP_1","Acc X"), ("DSP_2","Acc Y"),
                                          ("DSP_3","Acc Z")]:
                    raw = arrs.get(acc_key, np.array([]))
                    val = raw[~np.isnan(raw)]
                    if len(val) < 64:
                        continue
                    nperseg = min(1024, len(val))
                    freqs, psd = welch_psd(val, float(cps), nperseg=nperseg)
                    psd_g = psd / (9.80665 ** 2)
                    peak_idx = int(np.argmax(psd_g[1:])) + 1

                    fig = Figure(figsize=A4, facecolor=BG_DARK, tight_layout=True)
                    _hdr(fig, f"PSD — {acc_lbl}  (DSP_1)",
                         f"Método de Welch  |  fs = {cps} Hz  |  f_max = {cps/2:.1f} Hz  |  "
                         f"Pico: {freqs[peak_idx]:.2f} Hz")
                    fig.subplots_adjust(top=0.88, bottom=0.06, left=0.1, right=0.96)

                    ax1 = fig.add_subplot(2, 1, 1, facecolor=BG_PANEL)
                    t_full = _decimate(arrs["tiempo_s"], step) * scale
                    v_plot = _decimate(raw, step)
                    ax1.plot(t_full, v_plot, lw=0.8, color=ACCENT)
                    ax1.set_title("Señal temporal", color=FG_TEXT, fontsize=9)
                    ax1.set_xlabel(time_label, color=FG_MUTED, fontsize=8)
                    ax1.set_ylabel("m/s²", color=FG_MUTED, fontsize=8)
                    _style_axes(ax1)

                    ax2 = fig.add_subplot(2, 1, 2, facecolor=BG_PANEL)
                    ax2.semilogy(freqs[1:], psd_g[1:], lw=1.2, color=GREEN)
                    ax2.fill_between(freqs[1:], psd_g[1:], alpha=0.15, color=GREEN)
                    ax2.axvline(freqs[peak_idx], color=RED, lw=0.9, ls="--")
                    ax2.annotate(f"{freqs[peak_idx]:.2f} Hz",
                                 xy=(freqs[peak_idx], psd_g[peak_idx]),
                                 xytext=(8, 0), textcoords="offset points",
                                 color=RED, fontsize=8)
                    ax2.set_title("PSD  (G²/Hz)  —  spec CEV 7.2", color=FG_TEXT, fontsize=9)
                    ax2.set_xlabel("Frecuencia (Hz)", color=FG_MUTED, fontsize=8)
                    ax2.set_ylabel("G² / Hz", color=FG_MUTED, fontsize=8)
                    _style_axes(ax2)
                    _save(fig, pdf)

                # ════════════════════════════════════════════════════
                # 15+.  VISTA INDIVIDUAL POR CANAL
                # ════════════════════════════════════════════════════
                indv_step = max(1, n_cycles // (MAX_PLOT_POINTS * 2))
                t_indv    = _decimate(arrs["tiempo_s"], indv_step) * scale

                # Canales ANA y DSP con datos reales
                indv_keys = [(k, k) for k in ANA_COLS + DSP_COLS
                             if k in all_stats]

                for key, _ in indv_keys:
                    s = all_stats[key]
                    val = self._apply_cal(
                        _decimate(arrs.get(key, np.array([])), indv_step), key)
                    valid = val[~np.isnan(val)]
                    if len(valid) == 0:
                        continue

                    # Etiquetas
                    ch_name = s["name"] or IMU_LABELS.get(key, key)
                    unit    = s["unit"] or (IMU_UNITS.get(key, "LSB")
                              if key.startswith("DSP") else "LSB")

                    color = ACCENT if key.startswith("ANA") else (
                            GREEN  if key.startswith("DSP") else ORANGE)

                    fig = Figure(figsize=A4, facecolor=BG_DARK, tight_layout=True)
                    _hdr(fig, f"Vista Individual — {key}",
                         ch_name + (f"  [{unit}]" if unit else ""))
                    fig.subplots_adjust(top=0.88, bottom=0.08, left=0.1, right=0.96)

                    ax = fig.add_subplot(1, 1, 1, facecolor=BG_PANEL)
                    if key.startswith("DIG"):
                        ax.fill_between(t_indv, val, step="post",
                                        color=color, alpha=0.75)
                        ax.plot(t_indv, val, drawstyle="steps-post",
                                color=color, lw=1.0)
                        ax.set_ylim(-0.1, 1.3)
                        ax.set_yticks([0, 1])
                        ax.set_yticklabels(["0  LOW","1  HIGH"],
                                           color=FG_MUTED, fontsize=8)
                    else:
                        ax.plot(t_indv, val, color=color, lw=1.0)
                        ax.fill_between(t_indv, val, alpha=0.08, color=color)
                        # Anotar mín/máx
                        idx_mx = int(np.nanargmax(val))
                        idx_mn = int(np.nanargmin(val))
                        for idx_a, txt_a, va_a in [
                                (idx_mx, f"máx {valid.max():.4g}", "bottom"),
                                (idx_mn, f"mín {valid.min():.4g}", "top")]:
                            ax.annotate(txt_a,
                                        xy=(t_indv[idx_a], val[idx_a]),
                                        xytext=(0, 10 if va_a=="bottom" else -10),
                                        textcoords="offset points",
                                        color=FG_MUTED, fontsize=7,
                                        ha="center", va=va_a,
                                        arrowprops=dict(arrowstyle="-",
                                                        color=FG_MUTED, lw=0.7))

                    ax.set_xlabel(time_label, color=FG_MUTED, fontsize=9)
                    ax.set_ylabel(unit, color=FG_MUTED, fontsize=9)
                    _style_axes(ax)

                    # Caja de estadísticas
                    stats_txt = (f"mín = {s['min']:.4g}   "
                                 f"máx = {s['max']:.4g}   "
                                 f"media = {s['mean']:.4g}   "
                                 f"σ = {s['std']:.4g}")
                    fig.text(0.5, 0.03, stats_txt, ha="center",
                             color=ACCENT, fontsize=8)
                    _save(fig, pdf)

                # Metadata
                d = pdf.infodict()
                d["Title"]   = f"Reporte FDR — {Path(bin_path).name}"
                d["Author"]  = "FDR Converter · CEV-FAA"
                d["Subject"] = f"Vuelo {fmt_names[fmt]} — {dur_s:.0f} s"

            self._set_status(f"Reporte guardado: {Path(path).name}", 100)
            messagebox.showinfo("Reporte generado",
                                f"PDF guardado exitosamente:\n{path}")

        except Exception as exc:
            self._set_status("Error al generar reporte.", 0)
            messagebox.showerror("Error al generar reporte", str(exc))

    _CEV_FORMAT_CHOICES = {
        1: "F1  —  3 000 fr/s  (75 ciclos/s)   40×17 words",
        2: "F2  —  3 000 fr/s  (75 ciclos/s)   40×11 words",
        3: "F3  —  100 fr/s   (10 ciclos/s)   10×6 words",
    }

    def _selected_cev_format(self) -> int:
        text = self._bitrate_var.get() if hasattr(self, "_bitrate_var") else ""
        if text.startswith("F2"):
            return 2
        if text.startswith("F3"):
            return 3
        return 1

    def _pick_cmd_port(self):
        """Pide el COM si el monitor RS-232 no está conectado."""
        dlg = tk.Toplevel(self)
        dlg.title("Puerto de comandos")
        dlg.configure(bg=BG_PANEL)
        dlg.resizable(False, False)
        dlg.transient(self)
        dlg.grab_set()

        ports = ["COM1"]
        try:
            import serial.tools.list_ports
            detected = [p.device for p in serial.tools.list_ports.comports()]
            if detected:
                ports = detected
        except Exception:
            pass
        preset = self._mon_port.get() if hasattr(self, "_mon_port") else ports[0]
        if preset not in ports:
            ports = [preset] + ports

        tk.Label(dlg, text="El monitor no está conectado.\nElegí el COM del UART de comandos.",
                 bg=BG_PANEL, fg=FG_TEXT, font=("Segoe UI", 9),
                 justify="left").pack(anchor="w", padx=14, pady=(12, 6))
        port_var = tk.StringVar(value=preset if preset in ports else ports[0])
        ttk.Combobox(dlg, textvariable=port_var, values=ports,
                     state="readonly", width=16).pack(padx=14, pady=(0, 10))
        chosen = {"port": None}

        def _ok():
            chosen["port"] = port_var.get().strip()
            dlg.destroy()

        def _cancel():
            dlg.destroy()

        row = tk.Frame(dlg, bg=BG_PANEL)
        row.pack(pady=(0, 12))
        ttk.Button(row, text="Usar este puerto", command=_ok).pack(side="left", padx=4)
        ttk.Button(row, text="Cancelar", command=_cancel).pack(side="left", padx=4)
        dlg.wait_window()
        if not chosen["port"]:
            return None
        return chosen["port"], 115200

    def _cmd_port_for_format(self):
        """Puerto ya abierto del monitor, o uno elegido en el hilo de la UI."""
        if getattr(self, "_mon_running", False) and self._mon_serial is not None:
            return self._mon_serial.port, self._mon_serial.baudrate, True
        picked = self._pick_cmd_port()
        if picked is None:
            return None
        port, baud = picked
        return port, baud, False

    def _cmd_transact(self, command: int, data: int, port: str, baud: int, pause_monitor: bool):
        """
        Envía un mensaje de 6 bytes (header 0xAA55, comando, dato) y espera
        la respuesta del mismo comando. Devuelve el dato de la respuesta,
        o None si no hubo trama válida.
        """
        import time
        import serial
        import struct

        own_port = None
        held = False
        try:
            if pause_monitor and self._mon_serial is not None:
                self._mon_io_lock.acquire()
                held = True
                ser = self._mon_serial
            else:
                ser = serial.Serial()
                ser.port = port
                ser.baudrate = baud
                ser.timeout = 0.05
                # DTR/RTS en alto resetean el DevKit. El monitor y el formateo
                # ya los dejan en bajo; esta apertura tiene que hacer lo mismo.
                ser.dtr = False
                ser.rts = False
                ser.open()
                own_port = ser
                time.sleep(0.05)

            ser.reset_input_buffer()
            ser.write(struct.pack("<HHH", 0xAA55, command, data & 0xFFFF))
            deadline = time.monotonic() + 0.8
            buf = bytearray()
            while time.monotonic() < deadline:
                n = ser.in_waiting
                if n:
                    buf.extend(ser.read(n))
                else:
                    time.sleep(0.01)
                pos = 0
                while True:
                    found = buf.find(b"\x55\xAA", pos)
                    if found < 0 or found + 6 > len(buf):
                        break
                    hdr, cmd, val = struct.unpack("<HHH", bytes(buf[found:found + 6]))
                    if hdr == 0xAA55 and cmd == command:
                        return val
                    pos = found + 2
            return None
        finally:
            if own_port is not None and own_port.is_open:
                own_port.close()
            if held:
                self._mon_io_lock.release()

    def _set_fdr_format_status(self, text: str, color: str):
        if hasattr(self, "_fdr_fmt_status"):
            self._fdr_fmt_status.config(text=text, fg=color)

    def _read_fdr_format(self):
        target = self._cmd_port_for_format()
        if target is None:
            return
        port, baud, pause_monitor = target

        def work():
            try:
                val = self._cmd_transact(0x0003, 0, port, baud, pause_monitor)
            except Exception as exc:
                self.after(0, lambda: messagebox.showerror(
                    "Leer formato", f"No se pudo hablar con el FDR:\n{exc}"))
                return
            def show():
                if val in (1, 2, 3):
                    self._bitrate_var.set(self._CEV_FORMAT_CHOICES[val])
                    self._set_fdr_format_status(
                        f"Formato activo en el FDR: {val}", GREEN)
                elif val is None:
                    self._set_fdr_format_status(
                        "Formato activo en el FDR: sin respuesta", RED)
                    messagebox.showwarning(
                        "Leer formato",
                        "El FDR no respondió al comando 3.\n"
                        "Revisá el puerto COM y que el equipo esté encendido.")
                else:
                    self._set_fdr_format_status(
                        "Formato activo en el FDR: error", RED)
                    messagebox.showerror(
                        "Leer formato",
                        f"El FDR respondió un valor inválido: {val}.")
            self.after(0, show)
        threading.Thread(target=work, daemon=True).start()

    def _write_fdr_format(self):
        fmt = self._selected_cev_format()
        if not messagebox.askyesno(
            "Guardar formato",
            f"¿Guardar el formato {fmt} en el FDR?\n\n"
            "Queda en la memoria no volátil. La grabación en curso no cambia: "
            "reiniciá el equipo y después empezá el vuelo."):
            return
        target = self._cmd_port_for_format()
        if target is None:
            return
        port, baud, pause_monitor = target

        def work():
            try:
                val = self._cmd_transact(0x0002, fmt, port, baud, pause_monitor)
            except Exception as exc:
                self.after(0, lambda: messagebox.showerror(
                    "Guardar formato", f"No se pudo hablar con el FDR:\n{exc}"))
                return
            def show():
                if val == 0x0001:
                    self._set_fdr_format_status(
                        f"Formato guardado en el FDR: {fmt}  (reiniciar para usarlo)",
                        GREEN)
                    messagebox.showinfo(
                        "Formato guardado",
                        f"El formato {fmt} quedó guardado.\n\n"
                        "Reiniciá el FDR antes de grabar. El archivo de la "
                        "grabación que ya está abierta sigue en el formato anterior.")
                else:
                    self._set_fdr_format_status(
                        "Formato activo en el FDR: no se pudo guardar", RED)
                    messagebox.showerror(
                        "Guardar formato",
                        "El FDR no confirmó el cambio. "
                        "Tiene que responder ACK al comando 2.")
            self.after(0, show)
        threading.Thread(target=work, daemon=True).start()

    # ──────────────────────────────────────────────────────────────────
    # ⚙  Pestaña Configuración / Calibración LSQ
    # ──────────────────────────────────────────────────────────────────
    def _build_config_tab(self):
        """Construye la pestaña fija de Configuración (no requiere archivo cargado)."""
        tab = self._tab_cfg

        # Configuración de la grilla guardada en memoria
        # _grid_cfg: {canal: {"name", "sensor", "rate": "fast"|"slow", "range_min", "range_max"}}
        if not hasattr(self, "_grid_cfg"):
            self._grid_cfg: dict = {}
        if not hasattr(self, "_bitrate_var"):
            self._bitrate_var = tk.StringVar(value="F1  —  3 000 fr/s  (75 ciclos/s)")

        # ── Layout: panel izquierdo (grilla) + panel derecho (LSQ) ──
        paned = tk.PanedWindow(tab, orient="horizontal", bg=BG_DARK,
                               sashwidth=6, sashrelief="flat")
        paned.pack(fill="both", expand=True)

        left  = tk.Frame(paned, bg=BG_DARK)
        right = tk.Frame(paned, bg=BG_DARK)
        paned.add(left,  minsize=360)
        paned.add(right, minsize=400)

        self._build_grid_panel(left)
        self._build_lsq_panel(right)

    # ── Panel izquierdo: Grilla de muestreo ───────────────────────────
    def _build_grid_panel(self, parent):
        tk.Label(parent, text="GRILLA DE MUESTREO  —  MAD",
                 bg=BG_DARK, fg=ACCENT, font=("Segoe UI", 10, "bold")).pack(
                     anchor="w", padx=12, pady=(10, 2))

        # Bit rate
        br_row = tk.Frame(parent, bg=BG_CARD)
        br_row.pack(fill="x", padx=12, pady=(0, 8))
        tk.Label(br_row, text="Formato / Bit rate:", bg=BG_CARD, fg=FG_MUTED,
                 font=("Segoe UI", 9)).pack(side="left", padx=(10, 6), pady=8)
        br_combo = ttk.Combobox(br_row, textvariable=self._bitrate_var, state="readonly",
                                font=("Segoe UI", 9), width=34,
                                values=[
                                    "F1  —  3 000 fr/s  (75 ciclos/s)   40×17 words",
                                    "F2  —  3 000 fr/s  (75 ciclos/s)   40×11 words",
                                    "F3  —  100 fr/s   (10 ciclos/s)   10×6 words",
                                ])
        br_combo.pack(side="left", pady=8, padx=(0, 10))

        fmt_row = tk.Frame(parent, bg=BG_CARD)
        fmt_row.pack(fill="x", padx=12, pady=(0, 8))
        self._fdr_fmt_status = tk.Label(
            fmt_row, text="Formato activo en el FDR: —",
            bg=BG_CARD, fg=FG_TEXT, font=("Segoe UI", 9))
        self._fdr_fmt_status.pack(side="left", padx=(10, 8), pady=6)
        ttk.Button(fmt_row, text="Leer del FDR",
                   command=self._read_fdr_format).pack(side="left", padx=(0, 6), pady=6)
        ttk.Button(fmt_row, text="Guardar en el FDR",
                   command=self._write_fdr_format).pack(side="left", pady=6)
        tk.Label(fmt_row, text="Se aplica al reiniciar el FDR, antes de grabar.",
                 bg=BG_CARD, fg=FG_MUTED, font=("Segoe UI", 8, "italic")).pack(
                     side="left", padx=(10, 6))

        # Tabla de canales
        hdr = tk.Frame(parent, bg=BG_DARK)
        hdr.pack(fill="x", padx=12, pady=(0, 2))
        tk.Label(hdr, text="Canales analógicos:", bg=BG_DARK, fg=FG_MUTED,
                 font=("Segoe UI", 8, "bold")).pack(side="left")

        tree_frame = tk.Frame(parent, bg=BG_DARK)
        tree_frame.pack(fill="both", expand=True, padx=12, pady=(0, 6))

        cols = ("canal", "nombre", "sensor", "tasa", "rmin", "rmax")
        self._grid_tree = ttk.Treeview(tree_frame, columns=cols, show="headings",
                                       height=18, selectmode="browse")
        for col, txt, w in [("canal","Canal",55), ("nombre","Nombre",110),
                             ("sensor","Sensor/Transductor",130),
                             ("tasa","Tasa",55), ("rmin","Rng mín",60), ("rmax","Rng máx",60)]:
            self._grid_tree.heading(col, text=txt)
            self._grid_tree.column(col, width=w, anchor="center" if col in ("canal","tasa") else "w")

        gsb = ttk.Scrollbar(tree_frame, orient="vertical", command=self._grid_tree.yview)
        self._grid_tree.configure(yscrollcommand=gsb.set)
        self._grid_tree.pack(side="left", fill="both", expand=True)
        gsb.pack(side="right", fill="y")

        self._grid_tree.bind("<Double-1>", self._edit_grid_row)
        self._grid_refresh()

        # Botones
        btn_row = tk.Frame(parent, bg=BG_DARK)
        btn_row.pack(fill="x", padx=12, pady=(0, 10))
        ttk.Button(btn_row, text="✏  Editar canal",
                   command=self._edit_grid_row).pack(side="left", padx=(0, 6))
        ttk.Button(btn_row, text="📂  Cargar config",
                   command=self._load_grid_cfg).pack(side="left", padx=(0, 6))
        ttk.Button(btn_row, text="💾  Guardar config",
                   command=self._save_grid_cfg).pack(side="left")

    def _grid_refresh(self):
        for row in self._grid_tree.get_children():
            self._grid_tree.delete(row)
        for i in range(1, 33):
            key = f"ANA_{i}"
            c   = self._grid_cfg.get(key, {})
            tasa = c.get("rate", "—")
            tasa_lbl = "Rápido" if tasa == "fast" else ("Lento" if tasa == "slow" else "—")
            self._grid_tree.insert("", "end", iid=key, values=(
                key,
                c.get("name", ""),
                c.get("sensor", ""),
                tasa_lbl,
                c.get("range_min", ""),
                c.get("range_max", ""),
            ))

    def _edit_grid_row(self, _event=None):
        sel = self._grid_tree.selection()
        if not sel:
            messagebox.showinfo("Sin selección", "Seleccioná un canal para editar.")
            return
        self._edit_channel_info(sel[0])

    def _edit_channel_info(self, key: str, on_done=None):
        """
        Editor de canal con catálogo de sensores de aeronave.
        Usado desde la grilla de Configuración y desde el Monitor RS-232.
        """
        c      = self._grid_cfg.get(key, {})
        is_ana = key.startswith("ANA")

        dlg = tk.Toplevel(self)
        dlg.title(f"Editar {key}")
        dlg.configure(bg=BG_DARK)
        dlg.geometry("420x400" if is_ana else "420x240")
        dlg.resizable(False, False)
        dlg.grab_set()

        name_var   = tk.StringVar(value=c.get("name", ""))
        sensor_var = tk.StringVar(value=c.get("sensor", ""))
        unit_var   = tk.StringVar(value=c.get("unit", ""))
        rmin_var   = tk.StringVar(value=str(c.get("range_min", "")))
        rmax_var   = tk.StringVar(value=str(c.get("range_max", "")))
        rate_var   = tk.StringVar(value=c.get("rate", "fast"))

        frm = tk.Frame(dlg, bg=BG_DARK)
        frm.pack(fill="x", padx=16, pady=14)
        tk.Label(frm, text=key, bg=BG_DARK, fg=ACCENT,
                 font=("Segoe UI", 12, "bold")).grid(row=0, column=0, columnspan=2,
                                                      sticky="w", pady=(0, 10))

        def _row_label(text, r):
            tk.Label(frm, text=text, bg=BG_DARK, fg=FG_MUTED,
                     font=("Segoe UI", 9), width=14, anchor="e").grid(
                         row=r, column=0, padx=(0, 8), pady=4, sticky="e")

        r = 1
        if is_ana:
            # Combo de sensores del catálogo (editable para valores propios)
            _row_label("Sensor:", r)
            sensor_combo = ttk.Combobox(frm, textvariable=sensor_var,
                                        values=list(SENSOR_CATALOG.keys()),
                                        font=("Segoe UI", 9), width=28)
            sensor_combo.grid(row=r, column=1, sticky="w")

            def _on_sensor_selected(_e=None):
                sel_sensor = sensor_var.get()
                default_unit = SENSOR_CATALOG.get(sel_sensor)
                if default_unit:
                    unit_var.set(default_unit)
                if not name_var.get().strip() and sel_sensor != "Otro / personalizado":
                    name_var.set(sel_sensor)

            sensor_combo.bind("<<ComboboxSelected>>", _on_sensor_selected)
            r += 1

        _row_label("Nombre:", r)
        tk.Entry(frm, textvariable=name_var, bg=BG_PANEL, fg=FG_TEXT,
                 insertbackground=FG_TEXT, relief="flat",
                 font=("Segoe UI", 9), width=30).grid(row=r, column=1, sticky="w")
        r += 1

        _row_label("Unidad:", r)
        all_units = sorted({u for u in SENSOR_CATALOG.values() if u})
        ttk.Combobox(frm, textvariable=unit_var, values=all_units,
                     font=("Segoe UI", 9), width=10).grid(row=r, column=1, sticky="w")
        r += 1

        if is_ana:
            _row_label("Rango mínimo:", r)
            tk.Entry(frm, textvariable=rmin_var, bg=BG_PANEL, fg=FG_TEXT,
                     insertbackground=FG_TEXT, relief="flat",
                     font=("Segoe UI", 9), width=14).grid(row=r, column=1, sticky="w")
            r += 1
            _row_label("Rango máximo:", r)
            tk.Entry(frm, textvariable=rmax_var, bg=BG_PANEL, fg=FG_TEXT,
                     insertbackground=FG_TEXT, relief="flat",
                     font=("Segoe UI", 9), width=14).grid(row=r, column=1, sticky="w")
            r += 1
            _row_label("Tasa:", r)
            rate_row = tk.Frame(frm, bg=BG_DARK)
            rate_row.grid(row=r, column=1, sticky="w")
            for val, txt in [("fast", "Rápido"), ("slow", "Lento")]:
                tk.Radiobutton(rate_row, text=txt, variable=rate_var, value=val,
                               bg=BG_DARK, fg=FG_TEXT, selectcolor=BG_PANEL,
                               activebackground=BG_DARK,
                               font=("Segoe UI", 9)).pack(side="left", padx=4)
            r += 1

        def _save():
            if is_ana:
                try:
                    rmin = float(rmin_var.get()) if rmin_var.get().strip() else ""
                    rmax = float(rmax_var.get()) if rmax_var.get().strip() else ""
                except ValueError:
                    messagebox.showerror("Error", "Rango mín/máx deben ser números.",
                                         parent=dlg)
                    return
            else:
                rmin = rmax = ""

            self._grid_cfg[key] = {
                "name":      name_var.get().strip(),
                "sensor":    sensor_var.get().strip(),
                "unit":      unit_var.get().strip(),
                "rate":      rate_var.get() if is_ana else "",
                "range_min": rmin,
                "range_max": rmax,
            }
            # Sincronizar nombre/unidad con la calibración si existe
            if key in self._cal_data:
                if not self._cal_data[key].get("name"):
                    self._cal_data[key]["name"] = self._grid_cfg[key]["name"]
                if not self._cal_data[key].get("unit"):
                    self._cal_data[key]["unit"] = self._grid_cfg[key]["unit"]
            self._grid_refresh()
            if on_done:
                on_done(key)
            dlg.destroy()

        tk.Frame(dlg, bg=BG_DARK).pack(expand=True)
        btn_row = tk.Frame(dlg, bg=BG_DARK)
        btn_row.pack(pady=10)
        ttk.Button(btn_row, text="Guardar", style="Accent.TButton",
                   command=_save).pack(side="left", padx=6)
        ttk.Button(btn_row, text="Cancelar",
                   command=dlg.destroy).pack(side="left", padx=6)

    def _load_grid_cfg(self):
        path = filedialog.askopenfilename(
            title="Cargar configuración de grilla",
            filetypes=[("JSON", "*.json"), ("Todos", "*.*")])
        if not path:
            return
        try:
            with open(path, "r", encoding="utf-8") as f:
                data = json.load(f)
            self._grid_cfg = data.get("grid", {})
            br = data.get("bitrate", "")
            if br:
                self._bitrate_var.set(br)
            self._grid_refresh()
            messagebox.showinfo("Listo", f"Configuración cargada: {len(self._grid_cfg)} canal(es).")
        except Exception as exc:
            messagebox.showerror("Error al cargar", str(exc))

    def _save_grid_cfg(self):
        path = filedialog.asksaveasfilename(
            title="Guardar configuración de grilla",
            defaultextension=".json",
            filetypes=[("JSON", "*.json"), ("Todos", "*.*")])
        if not path:
            return
        try:
            with open(path, "w", encoding="utf-8") as f:
                json.dump({"version": 1,
                           "bitrate": self._bitrate_var.get(),
                           "grid":    self._grid_cfg}, f, ensure_ascii=False, indent=2)
            messagebox.showinfo("Guardado", f"Configuración guardada:\n{path}")
        except Exception as exc:
            messagebox.showerror("Error al guardar", str(exc))

    # ── Panel derecho: Mínimos Cuadrados ──────────────────────────────
    def _build_lsq_panel(self, parent):
        tk.Label(parent, text="CALIBRACIÓN  —  MÍNIMOS CUADRADOS",
                 bg=BG_DARK, fg=ACCENT, font=("Segoe UI", 10, "bold")).pack(
                     anchor="w", padx=12, pady=(10, 2))

        ctrl = tk.Frame(parent, bg=BG_CARD)
        ctrl.pack(fill="x", padx=12, pady=(0, 6))

        # Canal + grado
        row1 = tk.Frame(ctrl, bg=BG_CARD)
        row1.pack(fill="x", padx=10, pady=(8, 4))

        tk.Label(row1, text="Canal:", bg=BG_CARD, fg=FG_MUTED,
                 font=("Segoe UI", 9)).pack(side="left", padx=(0, 4))
        lsq_chan_opts = [f"ANA_{i}" for i in range(1, 33)]
        self._lsq_chan = tk.StringVar(value="ANA_1")
        ttk.Combobox(row1, textvariable=self._lsq_chan, values=lsq_chan_opts,
                     state="readonly", font=("Segoe UI", 9), width=10).pack(side="left", padx=(0, 16))

        tk.Label(row1, text="Grado:", bg=BG_CARD, fg=FG_MUTED,
                 font=("Segoe UI", 9)).pack(side="left", padx=(0, 4))
        self._lsq_deg = tk.IntVar(value=1)
        for d, txt in [(1, "1  (lineal)"), (2, "2"), (3, "3")]:
            tk.Radiobutton(row1, text=txt, variable=self._lsq_deg, value=d,
                           bg=BG_CARD, fg=FG_TEXT, selectcolor=BG_PANEL,
                           activebackground=BG_CARD, font=("Segoe UI", 9)).pack(side="left", padx=3)

        # Tabla de puntos
        tk.Label(parent, text="Tabla de puntos de calibración  (doble clic = editar)",
                 bg=BG_DARK, fg=FG_MUTED, font=("Segoe UI", 8, "bold")).pack(
                     anchor="w", padx=12, pady=(4, 2))

        pt_frame = tk.Frame(parent, bg=BG_DARK)
        pt_frame.pack(fill="x", padx=12, pady=(0, 4))

        pt_cols = ("n", "lsb", "real")
        self._pt_tree = ttk.Treeview(pt_frame, columns=pt_cols, show="headings",
                                     height=7, selectmode="browse")
        self._pt_tree.heading("n",    text="#")
        self._pt_tree.heading("lsb",  text="Valor LSB  (entrada)")
        self._pt_tree.heading("real", text="Valor real  (salida)")
        self._pt_tree.column("n",    width=30,  anchor="center")
        self._pt_tree.column("lsb",  width=140, anchor="center")
        self._pt_tree.column("real", width=140, anchor="center")

        pt_sb = ttk.Scrollbar(pt_frame, orient="vertical", command=self._pt_tree.yview)
        self._pt_tree.configure(yscrollcommand=pt_sb.set)
        self._pt_tree.pack(side="left", fill="x", expand=True)
        pt_sb.pack(side="right", fill="y")

        self._lsq_points: list[tuple] = []   # [(lsb, real), ...]
        self._pt_tree.bind("<Double-1>", self._edit_pt_row)

        # Botones de puntos
        pt_btns = tk.Frame(parent, bg=BG_DARK)
        pt_btns.pack(fill="x", padx=12, pady=(0, 6))
        ttk.Button(pt_btns, text="➕  Agregar punto",
                   command=self._add_pt_row).pack(side="left", padx=(0, 6))
        ttk.Button(pt_btns, text="🗑  Eliminar",
                   command=self._del_pt_row).pack(side="left", padx=(0, 6))
        ttk.Button(pt_btns, text="📂  Cargar CSV",
                   command=self._load_pts_csv).pack(side="left")

        # Resultado + botón calcular
        res_row = tk.Frame(parent, bg=BG_DARK)
        res_row.pack(fill="x", padx=12, pady=(0, 4))
        ttk.Button(res_row, text="📐  Calcular coeficientes",
                   style="Accent.TButton",
                   command=self._run_lsq).pack(side="left", padx=(0, 12))
        ttk.Button(res_row, text="💾  Aplicar a calibración",
                   command=self._lsq_apply_to_cal).pack(side="left")

        self._lsq_result_lbl = tk.Label(parent, text="", bg=BG_DARK, fg=YELLOW,
                                        font=("Segoe UI", 9, "bold"), wraplength=380,
                                        justify="left")
        self._lsq_result_lbl.pack(anchor="w", padx=12, pady=(0, 4))

        # Área del gráfico LSQ
        self._lsq_plot_frame = tk.Frame(parent, bg=BG_DARK)
        self._lsq_plot_frame.pack(fill="both", expand=True, padx=12, pady=(0, 10))
        tk.Label(self._lsq_plot_frame,
                 text="Agregá puntos de calibración y presioná  📐 Calcular",
                 bg=BG_DARK, fg=FG_MUTED, font=("Segoe UI", 10)).pack(expand=True)

        self._lsq_coeffs: list[float] = []   # resultado del polyfit

    def _pt_refresh(self):
        for row in self._pt_tree.get_children():
            self._pt_tree.delete(row)
        for i, (x, y) in enumerate(self._lsq_points, 1):
            self._pt_tree.insert("", "end", iid=str(i),
                                 values=(i, f"{x:.6g}", f"{y:.6g}"))

    def _add_pt_row(self):
        self._pt_dialog(None)

    def _del_pt_row(self):
        sel = self._pt_tree.selection()
        if not sel:
            return
        idx = int(sel[0]) - 1
        if 0 <= idx < len(self._lsq_points):
            self._lsq_points.pop(idx)
            self._pt_refresh()

    def _edit_pt_row(self, _event=None):
        sel = self._pt_tree.selection()
        idx = (int(sel[0]) - 1) if sel else None
        self._pt_dialog(idx)

    def _pt_dialog(self, idx):
        existing = self._lsq_points[idx] if idx is not None and idx < len(self._lsq_points) else None
        dlg = tk.Toplevel(self)
        dlg.title("Punto de calibración")
        dlg.configure(bg=BG_DARK)
        dlg.geometry("280x180")
        dlg.resizable(False, False)
        dlg.grab_set()

        x_var = tk.StringVar(value=str(existing[0]) if existing else "")
        y_var = tk.StringVar(value=str(existing[1]) if existing else "")

        frm = tk.Frame(dlg, bg=BG_DARK)
        frm.pack(fill="x", padx=20, pady=20)
        for r, (lbl_txt, var) in enumerate([("Valor LSB (x):", x_var),
                                             ("Valor real (y):", y_var)]):
            tk.Label(frm, text=lbl_txt, bg=BG_DARK, fg=FG_MUTED,
                     font=("Segoe UI", 9), width=16, anchor="e").grid(
                         row=r, column=0, padx=(0, 8), pady=6, sticky="e")
            tk.Entry(frm, textvariable=var, bg=BG_PANEL, fg=FG_TEXT,
                     insertbackground=FG_TEXT, relief="flat",
                     font=("Segoe UI", 9), width=14).grid(row=r, column=1, sticky="w")

        def _ok():
            try:
                xv = float(x_var.get())
                yv = float(y_var.get())
            except ValueError:
                messagebox.showerror("Error", "Ingresá valores numéricos.", parent=dlg)
                return
            if idx is not None and idx < len(self._lsq_points):
                self._lsq_points[idx] = (xv, yv)
            else:
                self._lsq_points.append((xv, yv))
            self._pt_refresh()
            dlg.destroy()

        btn_row = tk.Frame(dlg, bg=BG_DARK)
        btn_row.pack(pady=8)
        ttk.Button(btn_row, text="OK", style="Accent.TButton",
                   command=_ok).pack(side="left", padx=6)
        ttk.Button(btn_row, text="Cancelar",
                   command=dlg.destroy).pack(side="left", padx=6)

    def _load_pts_csv(self):
        path = filedialog.askopenfilename(
            title="Cargar puntos de calibración",
            filetypes=[("CSV", "*.csv"), ("Todos", "*.*")])
        if not path:
            return
        try:
            loaded = []
            with open(path, newline="", encoding="utf-8-sig") as f:
                reader = csv.reader(f)
                for row in reader:
                    if len(row) < 2:
                        continue
                    try:
                        loaded.append((float(row[0]), float(row[1])))
                    except ValueError:
                        pass   # saltar encabezados o líneas inválidas
            if not loaded:
                messagebox.showwarning("Sin datos",
                                       "No se encontraron pares numéricos en el archivo.")
                return
            self._lsq_points = loaded
            self._pt_refresh()
            messagebox.showinfo("Listo", f"{len(loaded)} punto(s) cargados.")
        except Exception as exc:
            messagebox.showerror("Error al cargar", str(exc))

    def _run_lsq(self):
        if len(self._lsq_points) < 2:
            messagebox.showwarning("Pocos puntos",
                                   "Necesitás al menos 2 puntos para ajustar.")
            return
        deg = self._lsq_deg.get()
        if len(self._lsq_points) <= deg:
            messagebox.showwarning("Pocos puntos",
                                   f"Para grado {deg} necesitás al menos {deg+1} puntos.")
            return

        xs = np.array([p[0] for p in self._lsq_points])
        ys = np.array([p[1] for p in self._lsq_points])

        coeffs = np.polyfit(xs, ys, deg)
        self._lsq_coeffs = list(coeffs)

        # Calcular R²
        y_fit  = np.polyval(coeffs, xs)
        ss_res = np.sum((ys - y_fit) ** 2)
        ss_tot = np.sum((ys - ys.mean()) ** 2)
        r2     = 1.0 - ss_res / ss_tot if ss_tot > 0 else 1.0

        # Construir string de la fórmula
        terms = []
        for i, c in enumerate(coeffs):
            exp = len(coeffs) - 1 - i
            if exp == 0:
                terms.append(f"{c:+.6g}")
            elif exp == 1:
                terms.append(f"{c:+.6g}·x")
            else:
                terms.append(f"{c:+.6g}·x^{exp}")
        formula = "y = " + "  ".join(terms)
        self._lsq_result_lbl.config(
            text=f"{formula}\n  R² = {r2:.6f}  —  {len(self._lsq_points)} puntos"
        )

        # Graficar
        self._draw_lsq_plot(xs, ys, coeffs, r2)

    def _draw_lsq_plot(self, xs, ys, coeffs, r2):
        for w in self._lsq_plot_frame.winfo_children():
            w.destroy()

        x_curve = np.linspace(xs.min(), xs.max(), 400)
        y_curve  = np.polyval(coeffs, x_curve)

        fig = Figure(figsize=(6, 3.2), facecolor=BG_DARK, tight_layout=True)
        ax  = fig.add_subplot(1, 1, 1, facecolor=BG_PANEL)

        ax.scatter(xs, ys, color=ACCENT, s=40, zorder=5, label="Puntos medidos")
        ax.plot(x_curve, y_curve, color=GREEN, lw=1.5, label=f"Ajuste  R²={r2:.4f}")
        key = self._lsq_chan.get()
        unit = (self._cal_data.get(key, {}).get("unit", "") or
                self._grid_cfg.get(key, {}).get("unit", "") or
                "Valor real")
        ax.set_xlabel("Valor LSB", color=FG_MUTED, fontsize=8)
        ax.set_ylabel(unit, color=FG_MUTED, fontsize=8)
        ax.set_title(f"Ajuste por mínimos cuadrados  —  {self._lsq_chan.get()}",
                     color=FG_TEXT, fontsize=9)
        _style_axes(ax)
        ax.legend(fontsize=7, facecolor=BG_CARD, edgecolor="none", labelcolor=FG_TEXT)

        self._embed_figure(fig, self._lsq_plot_frame)

    def _lsq_apply_to_cal(self):
        if not self._lsq_coeffs:
            messagebox.showwarning("Sin resultado",
                                   "Calculá los coeficientes primero con  📐 Calcular.")
            return
        key  = self._lsq_chan.get()
        deg  = self._lsq_deg.get()
        name = self._grid_cfg.get(key, {}).get("name", "")
        unit = (self._cal_data.get(key, {}).get("unit", "") or
                self._grid_cfg.get(key, {}).get("unit", ""))

        self._cal_data[key] = {
            "name":   name,
            "unit":   unit,
            "mode":   "linear" if deg == 1 else "poly",
            "degree": deg,
            "coeffs": self._lsq_coeffs,
        }
        messagebox.showinfo(
            "Aplicado",
            f"Coeficientes de {key} guardados en la calibración.\n"
            "Podés activar 'Mostrar valores calibrados' para verlos en los gráficos.")

    # ──────────────────────────────────────────────────────────────────
    # Calibración de canales
    # ──────────────────────────────────────────────────────────────────
    def _open_cal_dialog(self):
        dlg = tk.Toplevel(self)
        dlg.title("Calibración de canales analógicos")
        dlg.configure(bg=BG_DARK)
        dlg.geometry("880x560")
        dlg.minsize(760, 480)
        dlg.grab_set()

        # ── Tabla izquierda ──────────────────────────────────────────
        left = tk.Frame(dlg, bg=BG_DARK, width=340)
        left.pack(side="left", fill="y", padx=(12, 6), pady=12)
        left.pack_propagate(False)

        tk.Label(left, text="CANALES  (doble clic = editar)",
                 bg=BG_DARK, fg=FG_MUTED, font=("Segoe UI", 8, "bold")).pack(anchor="w", pady=(0, 4))

        cols = ("canal", "nombre", "unidad", "modo", "coefs")
        tree = ttk.Treeview(left, columns=cols, show="headings",
                            height=20, selectmode="browse")
        tree.heading("canal",  text="Canal")
        tree.heading("nombre", text="Nombre")
        tree.heading("unidad", text="Unidad")
        tree.heading("modo",   text="Modo")
        tree.heading("coefs",  text="Coeficientes")
        tree.column("canal",  width=55,  anchor="center")
        tree.column("nombre", width=100, anchor="w")
        tree.column("unidad", width=55,  anchor="center")
        tree.column("modo",   width=55,  anchor="center")
        tree.column("coefs",  width=150, anchor="w")

        sb = ttk.Scrollbar(left, orient="vertical", command=tree.yview)
        tree.configure(yscrollcommand=sb.set)
        tree.pack(side="left", fill="both", expand=True)
        sb.pack(side="right", fill="y")

        # Poblar con ANA_1…ANA_32
        def _refresh_tree():
            for row in tree.get_children():
                tree.delete(row)
            for i in range(1, 33):
                key = f"ANA_{i}"
                c   = self._cal_data.get(key, {})
                coef_str = ", ".join(f"{v:.6g}" for v in c.get("coeffs", [])) if c.get("coeffs") else "—"
                tree.insert("", "end", iid=key, values=(
                    key,
                    c.get("name", ""),
                    c.get("unit", ""),
                    c.get("mode", "linear"),
                    coef_str,
                ))
        _refresh_tree()

        # ── Panel derecho: editor ─────────────────────────────────────
        right = tk.Frame(dlg, bg=BG_CARD)
        right.pack(side="left", fill="both", expand=True, padx=(0, 12), pady=12)

        tk.Label(right, text="EDITOR DE CANAL", bg=BG_CARD, fg=FG_MUTED,
                 font=("Segoe UI", 8, "bold")).pack(anchor="w", padx=14, pady=(10, 6))

        ed_grid = tk.Frame(right, bg=BG_CARD)
        ed_grid.pack(fill="x", padx=14)

        def lbl(text, row):
            tk.Label(ed_grid, text=text, bg=BG_CARD, fg=FG_MUTED,
                     font=("Segoe UI", 9), width=12, anchor="e").grid(
                         row=row, column=0, padx=(0, 8), pady=4, sticky="e")

        ed_canal  = tk.StringVar()
        ed_nombre = tk.StringVar()
        ed_unidad = tk.StringVar()
        ed_modo   = tk.StringVar(value="linear")
        ed_grado  = tk.IntVar(value=2)
        ed_coefs  = tk.StringVar()

        lbl("Canal:", 0)
        tk.Label(ed_grid, textvariable=ed_canal, bg=BG_CARD, fg=ACCENT,
                 font=("Segoe UI", 10, "bold")).grid(row=0, column=1, sticky="w")

        lbl("Nombre:", 1)
        tk.Entry(ed_grid, textvariable=ed_nombre, bg=BG_PANEL, fg=FG_TEXT,
                 insertbackground=FG_TEXT, relief="flat",
                 font=("Segoe UI", 9), width=26).grid(row=1, column=1, sticky="w")

        lbl("Unidad:", 2)
        tk.Entry(ed_grid, textvariable=ed_unidad, bg=BG_PANEL, fg=FG_TEXT,
                 insertbackground=FG_TEXT, relief="flat",
                 font=("Segoe UI", 9), width=12).grid(row=2, column=1, sticky="w")

        lbl("Modo:", 3)
        modo_row = tk.Frame(ed_grid, bg=BG_CARD)
        modo_row.grid(row=3, column=1, sticky="w")
        for val, lbl_txt in [("linear", "Lineal  (y = a·x + b)"),
                              ("poly",   "Polinomial")]:
            tk.Radiobutton(modo_row, text=lbl_txt, variable=ed_modo, value=val,
                           bg=BG_CARD, fg=FG_TEXT, selectcolor=BG_PANEL,
                           activebackground=BG_CARD, font=("Segoe UI", 9),
                           command=lambda: _update_hint()).pack(anchor="w")

        lbl("Grado (poly):", 4)
        deg_row = tk.Frame(ed_grid, bg=BG_CARD)
        deg_row.grid(row=4, column=1, sticky="w")
        for d in (2, 3, 4):
            tk.Radiobutton(deg_row, text=str(d), variable=ed_grado, value=d,
                           bg=BG_CARD, fg=FG_TEXT, selectcolor=BG_PANEL,
                           activebackground=BG_CARD, font=("Segoe UI", 9),
                           command=lambda: _update_hint()).pack(side="left", padx=4)

        lbl("Coeficientes:", 5)
        coef_entry = tk.Entry(ed_grid, textvariable=ed_coefs, bg=BG_PANEL, fg=FG_TEXT,
                              insertbackground=FG_TEXT, relief="flat",
                              font=("Segoe UI", 9), width=34)
        coef_entry.grid(row=5, column=1, sticky="w")

        hint_lbl = tk.Label(right, text="", bg=BG_CARD, fg=FG_MUTED,
                            font=("Segoe UI", 8, "italic"), wraplength=440, justify="left")
        hint_lbl.pack(anchor="w", padx=14, pady=(4, 0))

        def _update_hint(*_):
            if ed_modo.get() == "linear":
                hint_lbl.config(text="Lineal: ingresá  a, b  (separados por coma)  →  y = a·x + b")
            else:
                d = ed_grado.get()
                terms = "  +  ".join(f"a{d-i}·x^{d-i}" if d-i > 1
                                     else ("a1·x" if d-i == 1 else "a0")
                                     for i in range(d + 1))
                hint_lbl.config(
                    text=f"Polinomial grado {d}: ingresá {d+1} coeficientes de mayor a menor\n"
                         f"y = {terms}")

        _update_hint()

        # Previsualización de la fórmula
        prev_lbl = tk.Label(right, text="", bg=BG_CARD, fg=YELLOW,
                            font=("Segoe UI", 9, "bold"))
        prev_lbl.pack(anchor="w", padx=14, pady=(2, 0))

        def _preview_formula(*_):
            raw = ed_coefs.get().strip()
            if not raw:
                prev_lbl.config(text="")
                return
            try:
                c = [float(x.strip()) for x in raw.replace(";", ",").split(",")]
                if len(c) < 2:
                    prev_lbl.config(text="")
                    return
                terms = []
                for i, coef in enumerate(c):
                    exp = len(c) - 1 - i
                    if exp == 0:
                        terms.append(f"{coef:+.4g}")
                    elif exp == 1:
                        terms.append(f"{coef:+.4g}·x")
                    else:
                        terms.append(f"{coef:+.4g}·x^{exp}")
                prev_lbl.config(text="y = " + "  ".join(terms))
            except ValueError:
                prev_lbl.config(text="⚠  Coeficientes inválidos")

        ed_coefs.trace_add("write", lambda *_: _preview_formula())

        # ── Botones del editor ──
        btn_ed = tk.Frame(right, bg=BG_CARD)
        btn_ed.pack(anchor="w", padx=14, pady=(12, 4))

        def _load_selected():
            sel = tree.selection()
            if not sel:
                return
            key = sel[0]
            c   = self._cal_data.get(key, {})
            ed_canal.set(key)
            ed_nombre.set(c.get("name", ""))
            ed_unidad.set(c.get("unit", ""))
            ed_modo.set(c.get("mode", "linear"))
            ed_grado.set(c.get("degree", 2))
            ed_coefs.set(", ".join(f"{v:.6g}" for v in c.get("coeffs", [])))
            _update_hint()
            _preview_formula()

        def _save_channel():
            key = ed_canal.get()
            if not key:
                messagebox.showwarning("Sin canal", "Seleccioná un canal de la tabla primero.",
                                       parent=dlg)
                return
            raw = ed_coefs.get().strip()
            try:
                coeffs = [float(x.strip()) for x in raw.replace(";", ",").split(",") if x.strip()]
            except ValueError:
                messagebox.showerror("Error", "Coeficientes inválidos. Usá números separados por coma.",
                                     parent=dlg)
                return
            mode = ed_modo.get()
            expected = 2 if mode == "linear" else (ed_grado.get() + 1)
            if len(coeffs) != expected:
                messagebox.showerror(
                    "Error",
                    f"Modo {mode} requiere {expected} coeficiente(s), "
                    f"pero se ingresaron {len(coeffs)}.",
                    parent=dlg)
                return
            self._cal_data[key] = {
                "name":   ed_nombre.get().strip(),
                "unit":   ed_unidad.get().strip(),
                "mode":   mode,
                "degree": ed_grado.get() if mode == "poly" else 1,
                "coeffs": coeffs,
            }
            _refresh_tree()
            tree.selection_set(key)
            tree.see(key)

        def _clear_channel():
            key = ed_canal.get()
            if key and key in self._cal_data:
                self._cal_data.pop(key)
            ed_nombre.set("")
            ed_unidad.set("")
            ed_coefs.set("")
            _refresh_tree()

        ttk.Button(btn_ed, text="💾  Guardar canal",
                   style="Accent.TButton", command=_save_channel).pack(side="left", padx=(0, 6))
        ttk.Button(btn_ed, text="🗑  Limpiar",
                   command=_clear_channel).pack(side="left")

        tree.bind("<Double-1>", lambda _: _load_selected())
        tree.bind("<<TreeviewSelect>>", lambda _: _load_selected())

        # ── Separador + botones globales ──────────────────────────────
        tk.Frame(right, bg=BG_PANEL, height=1).pack(fill="x", padx=14, pady=10)

        glob_row = tk.Frame(right, bg=BG_CARD)
        glob_row.pack(anchor="w", padx=14)

        def _load_json():
            path = filedialog.askopenfilename(
                title="Cargar archivo de calibración",
                filetypes=[("JSON", "*.json"), ("Todos", "*.*")],
                parent=dlg)
            if not path:
                return
            try:
                with open(path, "r", encoding="utf-8") as f:
                    data = json.load(f)
                self._cal_data = data.get("channels", {})
                _refresh_tree()
                messagebox.showinfo("Listo",
                                    f"Calibración cargada: {len(self._cal_data)} canal(es).",
                                    parent=dlg)
            except Exception as exc:
                messagebox.showerror("Error al cargar", str(exc), parent=dlg)

        def _save_json():
            path = filedialog.asksaveasfilename(
                title="Guardar calibración",
                defaultextension=".json",
                filetypes=[("JSON", "*.json"), ("Todos", "*.*")],
                parent=dlg)
            if not path:
                return
            try:
                with open(path, "w", encoding="utf-8") as f:
                    json.dump({"version": 1, "channels": self._cal_data},
                              f, ensure_ascii=False, indent=2)
                messagebox.showinfo("Guardado", f"Calibración guardada en:\n{path}", parent=dlg)
            except Exception as exc:
                messagebox.showerror("Error al guardar", str(exc), parent=dlg)

        def _on_close():
            dlg.destroy()
            if self._cal_mode.get() and self._records:
                self._replot_with_cal()

        ttk.Button(glob_row, text="📂  Cargar JSON",
                   command=_load_json).pack(side="left", padx=(0, 6))
        ttk.Button(glob_row, text="💾  Guardar JSON",
                   command=_save_json).pack(side="left", padx=(0, 12))
        ttk.Button(glob_row, text="✔  Aplicar y cerrar",
                   style="Accent.TButton",
                   command=_on_close).pack(side="left")

    # ──────────────────────────────────────────────────────────────────
    # PSD / Vibración
    # ──────────────────────────────────────────────────────────────────
    def _setup_psd_tab(self, records, fmt):
        """Construye la pestaña PSD / Vibración."""
        self._clear_tab(self._tab_psd)
        self._psd_records = records
        self._psd_fmt     = fmt
        fs_map = {1: 75.0, 2: 75.0, 3: 10.0}
        self._psd_fs = fs_map[fmt]

        t_max = records[-1]["tiempo_s"] if records else 0.0

        # Armar opciones de canal
        n_fast = 16 if fmt in (1, 2) else 10
        psd_opts = []
        for i in range(1, 33):
            g = "Rápido" if i <= n_fast else "Lento"
            psd_opts.append((f"ANA_{i}", f"ANA {i}  —  Analógico {g}  [LSB]"))
        for i in range(1, 13):
            lbl   = IMU_LABELS.get(f"DSP_{i}", f"DSP {i}")
            units = IMU_UNITS.get(f"DSP_{i}", "")
            psd_opts.append((f"DSP_{i}", f"DSP {i}  —  {lbl}  [{units}]"))
        self._psd_key_map = {label: key for key, label in psd_opts}
        labels = [label for _, label in psd_opts]

        # ── Barra de control ──
        ctrl = tk.Frame(self._tab_psd, bg=BG_PANEL)
        ctrl.pack(fill="x", side="top")

        # Fila 1: canal + ventana + segmento
        row1 = tk.Frame(ctrl, bg=BG_PANEL)
        row1.pack(fill="x", padx=12, pady=(10, 4))

        tk.Label(row1, text="Canal:", bg=BG_PANEL, fg=FG_MUTED,
                 font=("Segoe UI", 9)).pack(side="left", padx=(0, 4))
        self._psd_combo = ttk.Combobox(row1, values=labels, state="readonly",
                                       font=("Segoe UI", 9), width=36)
        self._psd_combo.pack(side="left", padx=(0, 16))
        # Preseleccionar DSP_1 (Acc X) si está disponible
        default_lbl = next((l for l in labels if "DSP 1" in l), labels[0])
        self._psd_combo.current(labels.index(default_lbl))

        tk.Label(row1, text="Ventana:", bg=BG_PANEL, fg=FG_MUTED,
                 font=("Segoe UI", 9)).pack(side="left", padx=(0, 4))
        self._psd_win_var = tk.StringVar(value="hann")
        win_combo = ttk.Combobox(row1, textvariable=self._psd_win_var,
                                 values=["hann", "hamming", "blackman", "rectangular"],
                                 state="readonly", font=("Segoe UI", 9), width=10)
        win_combo.pack(side="left", padx=(0, 16))

        tk.Label(row1, text="Segmento:", bg=BG_PANEL, fg=FG_MUTED,
                 font=("Segoe UI", 9)).pack(side="left", padx=(0, 4))
        self._psd_seg_var = tk.StringVar(value="1024")
        seg_combo = ttk.Combobox(row1, textvariable=self._psd_seg_var,
                                 values=["256", "512", "1024", "2048", "4096"],
                                 state="readonly", font=("Segoe UI", 9), width=6)
        seg_combo.pack(side="left", padx=(0, 16))

        # Fila 2: rango de tiempo + botón
        row2 = tk.Frame(ctrl, bg=BG_PANEL)
        row2.pack(fill="x", padx=12, pady=(0, 10))

        tk.Label(row2, text="t inicio (s):", bg=BG_PANEL, fg=FG_MUTED,
                 font=("Segoe UI", 9)).pack(side="left", padx=(0, 4))
        self._psd_t0 = tk.StringVar(value="0")
        tk.Entry(row2, textvariable=self._psd_t0, bg=BG_PANEL, fg=FG_TEXT,
                 insertbackground=FG_TEXT, relief="flat",
                 font=("Segoe UI", 9), width=8).pack(side="left", padx=(0, 12))

        tk.Label(row2, text="t fin (s):", bg=BG_PANEL, fg=FG_MUTED,
                 font=("Segoe UI", 9)).pack(side="left", padx=(0, 4))
        self._psd_t1 = tk.StringVar(value=f"{t_max:.1f}")
        tk.Entry(row2, textvariable=self._psd_t1, bg=BG_PANEL, fg=FG_TEXT,
                 insertbackground=FG_TEXT, relief="flat",
                 font=("Segoe UI", 9), width=8).pack(side="left", padx=(0, 16))

        tk.Label(row2,
                 text=f"fs = {self._psd_fs:.0f} Hz  →  f_max = {self._psd_fs/2:.1f} Hz",
                 bg=BG_PANEL, fg=FG_MUTED, font=("Segoe UI", 8, "italic")).pack(side="left", padx=(0, 16))

        ttk.Button(row2, text="📊  Calcular PSD",
                   style="Accent.TButton",
                   command=self._calculate_psd).pack(side="left")

        self._psd_info = tk.Label(row2, text="", bg=BG_PANEL, fg=ACCENT,
                                  font=("Segoe UI", 8))
        self._psd_info.pack(side="left", padx=12)

        # Separador
        tk.Frame(self._tab_psd, bg=BG_CARD, height=1).pack(fill="x")

        # Área del gráfico
        self._psd_plot_frame = tk.Frame(self._tab_psd, bg=BG_DARK)
        self._psd_plot_frame.pack(fill="both", expand=True)

        tk.Label(self._psd_plot_frame,
                 text="Configurá los parámetros y presioná  📊 Calcular PSD",
                 bg=BG_DARK, fg=FG_MUTED, font=("Segoe UI", 11)).pack(expand=True)

    def _calculate_psd(self):
        """Lee parámetros del panel y lanza el cálculo + gráfico."""
        if not self._psd_records:
            return

        label = self._psd_combo.get()
        key   = self._psd_key_map.get(label)
        if not key:
            return

        try:
            t0 = float(self._psd_t0.get())
            t1 = float(self._psd_t1.get())
        except ValueError:
            messagebox.showerror("Error", "t inicio y t fin deben ser números.")
            return

        if t0 >= t1:
            messagebox.showerror("Error", "t inicio debe ser menor que t fin.")
            return

        nperseg = int(self._psd_seg_var.get())
        window  = self._psd_win_var.get()
        if window == "rectangular":
            window = "rect"

        arrs = self._arrays
        t_all = arrs["tiempo_s"]
        mask  = (t_all >= t0) & (t_all <= t1)
        t_seg = t_all[mask]
        val   = arrs.get(key, np.array([]))[mask]

        if len(val) < nperseg:
            messagebox.showwarning(
                "Segmento muy corto",
                f"El rango seleccionado tiene {len(val)} muestras pero el segmento es {nperseg}.\n"
                f"Reducí el tamaño de segmento o ampliá el rango de tiempo."
            )
            return

        freqs, psd = welch_psd(val, self._psd_fs, nperseg=nperseg, window=window)

        # Unidades del eje Y
        if key.startswith("ANA"):
            unit_lbl = "LSB"
            y_label  = "PSD  [LSB² / Hz]"
        elif key in IMU_UNITS:
            unit_lbl = IMU_UNITS[key]
            y_label  = f"PSD  [{unit_lbl}² / Hz]"
        else:
            unit_lbl = ""
            y_label  = "PSD  [u² / Hz]"

        # Si es acelerómetro → también mostrar en G²/Hz
        is_accel = key in ("DSP_1", "DSP_2", "DSP_3")

        # Info del pico dominante
        peak_idx  = int(np.argmax(psd[1:])) + 1
        peak_freq = freqs[peak_idx]
        peak_psd  = psd[peak_idx]
        self._psd_info.config(
            text=f"Pico: {peak_freq:.2f} Hz  ({peak_psd:.3g} {unit_lbl}²/Hz)"
        )

        self._draw_psd(t_seg, val, freqs, psd, label, y_label, is_accel, t0, t1)

    def _draw_psd(self, t_seg, val, freqs, psd, title, y_label, is_accel, t0, t1):
        """Dibuja la señal temporal + PSD en la pestaña."""
        for w in self._psd_plot_frame.winfo_children():
            w.destroy()

        rows = 3 if is_accel else 2
        fig  = Figure(figsize=(10, 6 if rows == 2 else 7),
                      facecolor=BG_DARK, tight_layout=True)

        # ── Subplot 1: señal en el tiempo ──
        scale, time_label, unit_suffix = self._get_time_scale_and_label()
        t_scaled = t_seg * scale
        ax_t = fig.add_subplot(rows, 1, 1, facecolor=BG_PANEL)
        ax_t.plot(t_scaled, val, lw=0.8, color=ACCENT)
        ax_t.set_title(f"Señal temporal  —  {title}  [{t0*scale:.2f} {unit_suffix} – {t1*scale:.2f} {unit_suffix}]",
                       color=FG_TEXT, fontsize=10)
        ax_t.set_xlabel(time_label, color=FG_MUTED, fontsize=8)
        ax_t.set_ylabel(y_label.split("[")[1].rstrip("]") if "[" in y_label else "",
                        color=FG_MUTED, fontsize=8)
        _style_axes(ax_t)

        # ── Subplot 2: PSD ──
        ax_f = fig.add_subplot(rows, 1, 2, facecolor=BG_PANEL)
        ax_f.semilogy(freqs[1:], psd[1:], lw=1.2, color=GREEN)
        ax_f.fill_between(freqs[1:], psd[1:], alpha=0.15, color=GREEN)

        # Marcar pico dominante
        peak_idx = int(np.argmax(psd[1:])) + 1
        ax_f.axvline(freqs[peak_idx], color=RED, lw=0.9, ls="--", alpha=0.8)
        ax_f.annotate(f"{freqs[peak_idx]:.2f} Hz",
                      xy=(freqs[peak_idx], psd[peak_idx]),
                      xytext=(8, 0), textcoords="offset points",
                      color=RED, fontsize=8, va="center")

        ax_f.set_title("Densidad Espectral de Potencia  (Welch)", color=FG_TEXT, fontsize=10)
        ax_f.set_xlabel("Frecuencia (Hz)", color=FG_MUTED, fontsize=8)
        ax_f.set_ylabel(y_label, color=FG_MUTED, fontsize=8)
        _style_axes(ax_f)

        # ── Subplot 3 (solo acelerómetros): PSD en G²/Hz ──
        if is_accel:
            G = 9.80665
            psd_g = psd / (G ** 2)
            ax_g  = fig.add_subplot(rows, 1, 3, facecolor=BG_PANEL)
            ax_g.semilogy(freqs[1:], psd_g[1:], lw=1.2, color=ORANGE)
            ax_g.fill_between(freqs[1:], psd_g[1:], alpha=0.15, color=ORANGE)
            ax_g.axvline(freqs[peak_idx], color=RED, lw=0.9, ls="--", alpha=0.8)
            ax_g.set_title("PSD  —  Aceleración en G²/Hz  (CEV spec 7.2)",
                           color=FG_TEXT, fontsize=10)
            ax_g.set_xlabel("Frecuencia (Hz)", color=FG_MUTED, fontsize=8)
            ax_g.set_ylabel("PSD  [G² / Hz]", color=FG_MUTED, fontsize=8)
            _style_axes(ax_g)

        self._fig_psd = fig
        self._embed_figure(fig, self._psd_plot_frame)

    # ──────────────────────────────────────────────────────────────────
    # Posición GPS (LAT, LON, ALT)
    # ──────────────────────────────────────────────────────────────────
    def _draw_gps_tab(self, records):
        self._clear_tab(self._tab_gps)
        arrs = self._arrays
        step = max(1, len(records) // MAX_PLOT_POINTS)
        scale, time_label, _ = self._get_time_scale_and_label()
        t    = _decimate(arrs["tiempo_s"], step) * scale

        lat = _decimate(arrs.get("LAT", np.array([])), step)
        lon = _decimate(arrs.get("LON", np.array([])), step)
        alt = _decimate(arrs.get("ALT", np.array([])), step)

        has_gps = (len(lat) > 0 and not np.all(np.isnan(lat)) and
                   len(lon) > 0 and not np.all(np.isnan(lon)))

        if not has_gps:
            self._fig_gps = None
            self._placeholder(self._tab_gps,
                              "Sin datos de posición GPS\n"
                              "(solo disponible en Formato 2 y Formato 3)")
            return

        fig = Figure(figsize=(9, 7), facecolor=BG_DARK, tight_layout=True)

        # ── Latitud vs tiempo ──
        ax1 = fig.add_subplot(3, 1, 1, facecolor=BG_PANEL)
        ax1.plot(t, lat, lw=0.9, color=ACCENT)
        ax1.set_title("Latitud", color=FG_TEXT, fontsize=10)
        ax1.set_ylabel("°", color=FG_MUTED, fontsize=8)
        _style_axes(ax1)

        # ── Longitud vs tiempo ──
        ax2 = fig.add_subplot(3, 1, 2, facecolor=BG_PANEL)
        ax2.plot(t, lon, lw=0.9, color=GREEN)
        ax2.set_title("Longitud", color=FG_TEXT, fontsize=10)
        ax2.set_ylabel("°", color=FG_MUTED, fontsize=8)
        _style_axes(ax2)

        # ── Altitud vs tiempo ──
        ax3 = fig.add_subplot(3, 1, 3, facecolor=BG_PANEL)
        ax3.plot(t, alt, lw=0.9, color=ORANGE)
        ax3.fill_between(t, alt, alpha=0.1, color=ORANGE)
        ax3.set_title("Altitud", color=FG_TEXT, fontsize=10)
        ax3.set_xlabel(time_label, color=FG_MUTED, fontsize=8)
        ax3.set_ylabel("m", color=FG_MUTED, fontsize=8)
        _style_axes(ax3)

        self._fig_gps = fig
        self._embed_figure(fig, self._tab_gps)

    def _draw_plots(self, records, fmt):
        arrs   = self._arrays
        n      = len(records)
        step   = max(1, n // MAX_PLOT_POINTS)
        scale, time_label, _ = self._get_time_scale_and_label()
        t      = _decimate(arrs["tiempo_s"], step) * scale
        freq_mode = self._freq_mode.get()
        xlabel = "Frecuencia (Hz)" if freq_mode else time_label
        dec_note = "" if freq_mode else (f"  (decimado ×{step})" if step > 1 else "")
        ana_ylabel = self._cal_ylabel("LSB", [f"ANA_{i}" for i in range(1, 17)])
        if freq_mode:
            ana_ylabel = f"|Y(f)|  [{ana_ylabel}]"

        # ── Analógicas ────────────────────────────────────────────────
        self._clear_tab(self._tab_ana)
        fig_ana = Figure(figsize=(9, 6), facecolor=BG_DARK, tight_layout=True)

        n_fast = 16 if fmt in (1, 2) else 10

        lined_ana = {}
        lines_ax1, lines_ax2 = [], []

        fast_keys = [f"ANA_{i}" for i in range(1, n_fast + 1)]
        slow_keys = [f"ANA_{i}" for i in range(n_fast + 1, 33)]

        ax1 = fig_ana.add_subplot(2, 1, 1, facecolor=BG_PANEL)
        for i in range(1, n_fast + 1):
            key = f"ANA_{i}"
            raw = arrs.get(key, np.array([]))
            v = self._apply_cal(raw if freq_mode else _decimate(raw, step), key)
            x, y = self._trace_xy(v, t)
            if len(y) and not np.all(np.isnan(y)):
                ln, = ax1.plot(x, y, lw=0.8, label=self._cal_label(key, f"ANA {i}"))
                lines_ax1.append(ln)
        ax1.set_title(
            f"{'Espectro — ' if freq_mode else ''}Canales Rápidos{dec_note}  —  clic en leyenda = mostrar/ocultar",
            color=FG_TEXT, fontsize=10)
        ax1.set_ylabel(ana_ylabel if freq_mode else self._cal_ylabel("LSB", fast_keys),
                       color=FG_MUTED, fontsize=8)
        _style_axes(ax1)
        leg1 = ax1.legend(loc="upper right", fontsize=7, ncol=4,
                          facecolor=BG_CARD, edgecolor="none", labelcolor=FG_TEXT)
        for legln, dataln in zip(leg1.get_lines(), lines_ax1):
            lined_ana[legln] = dataln

        ax2 = fig_ana.add_subplot(2, 1, 2, facecolor=BG_PANEL)
        for i in range(n_fast + 1, 33):
            key = f"ANA_{i}"
            raw = arrs.get(key, np.array([]))
            v = self._apply_cal(raw if freq_mode else _decimate(raw, step), key)
            x, y = self._trace_xy(v, t)
            if len(y) and not np.all(np.isnan(y)):
                ln, = ax2.plot(x, y, lw=0.8, label=self._cal_label(key, f"ANA {i}"))
                lines_ax2.append(ln)
        ax2.set_title(
            f"{'Espectro — ' if freq_mode else ''}Canales Lentos{dec_note}  —  clic en leyenda = mostrar/ocultar",
            color=FG_TEXT, fontsize=10)
        ax2.set_xlabel(xlabel, color=FG_MUTED, fontsize=8)
        ax2.set_ylabel(ana_ylabel if freq_mode else self._cal_ylabel("LSB", slow_keys),
                       color=FG_MUTED, fontsize=8)
        _style_axes(ax2)
        leg2 = ax2.legend(loc="upper right", fontsize=7, ncol=4,
                          facecolor=BG_CARD, edgecolor="none", labelcolor=FG_TEXT)
        for legln, dataln in zip(leg2.get_lines(), lines_ax2):
            lined_ana[legln] = dataln

        self._fig_ana  = fig_ana
        canvas_ana = self._embed_figure(fig_ana, self._tab_ana)
        self._make_legend_interactive(canvas_ana, fig_ana, lined_ana)

        # ── IMU ───────────────────────────────────────────────────────
        self._clear_tab(self._tab_imu)
        fig_imu = Figure(figsize=(9, 6), facecolor=BG_DARK, tight_layout=True)
        imu_groups = [
            ("Aceleración (m/s²)",      ["DSP_1", "DSP_2", "DSP_3"]),
            ("Giróscopo (°/s)",         ["DSP_4", "DSP_5", "DSP_6"]),
            ("Roll / Pitch / Yaw (°)",  ["DSP_10", "DSP_11", "DSP_13"]),
            ("Magnetómetro (a.u.)",     ["DSP_7", "DSP_8", "DSP_9"]),
        ]
        palette = [ACCENT, GREEN, ORANGE, RED, "#cba6f7", YELLOW, "#74c7ec", "#b4befe"]
        lined_imu = {}
        for idx, (title, keys) in enumerate(imu_groups, 1):
            ax     = fig_imu.add_subplot(2, 2, idx, facecolor=BG_PANEL)
            ax_lns = []
            for ci, key in enumerate(keys):
                raw = arrs.get(key, np.array([]))
                v = raw if freq_mode else _decimate(raw, step)
                x, y = self._trace_xy(v, t)
                if len(y) and not np.all(np.isnan(y)):
                    ln, = ax.plot(x, y, lw=0.9, label=IMU_LABELS.get(key, key),
                                  color=palette[ci % len(palette)])
                    ax_lns.append(ln)
            ax.set_title(("Espectro — " if freq_mode else "") + title,
                         color=FG_TEXT, fontsize=9)
            ax.set_xlabel(xlabel, color=FG_MUTED, fontsize=7)
            _style_axes(ax)
            leg = ax.legend(loc="upper right", fontsize=7,
                            facecolor=BG_CARD, edgecolor="none", labelcolor=FG_TEXT)
            for legln, dataln in zip(leg.get_lines(), ax_lns):
                lined_imu[legln] = dataln

        self._fig_imu  = fig_imu
        canvas_imu = self._embed_figure(fig_imu, self._tab_imu)
        self._make_legend_interactive(canvas_imu, fig_imu, lined_imu)

        # ── Discretas ─────────────────────────────────────────────────
        self._clear_tab(self._tab_dig)
        fig_dig = Figure(figsize=(9, 7), facecolor=BG_DARK, tight_layout=True)
        for i in range(1, 17):
            ax = fig_dig.add_subplot(4, 4, i, facecolor=BG_PANEL)
            v  = _decimate(arrs.get(f"DIG_{i}", np.array([])), step)
            if len(v):
                ax.plot(t, v, lw=0.9, color=ACCENT, drawstyle="steps-post")
            ax.set_ylim(-0.1, 1.2)
            ax.set_yticks([0, 1])
            ax.set_yticklabels(["0", "1"], fontsize=6, color=FG_MUTED)
            ax.set_title(f"D{i}", color=FG_TEXT, fontsize=8)
            _style_axes(ax, tiny=True)
        self._fig_dig  = fig_dig
        self._embed_figure(fig_dig, self._tab_dig)

        # ── Tiempo GNSS ───────────────────────────────────────────────
        self._clear_tab(self._tab_time)
        fig_time = Figure(figsize=(9, 4), facecolor=BG_DARK, tight_layout=True)
        t_full = arrs["tiempo_s"] * scale
        for idx, (key, title) in enumerate([("HOR","Hora"),("MIN","Minuto"),("SEG","Segundo")], 1):
            ax = fig_time.add_subplot(1, 3, idx, facecolor=BG_PANEL)
            v  = arrs.get(key, np.array([]))
            ax.plot(t_full, v, lw=0.9, color=YELLOW, drawstyle="steps-post")
            ax.set_title(title, color=FG_TEXT, fontsize=9)
            ax.set_xlabel(time_label, color=FG_MUTED, fontsize=8)
            _style_axes(ax)
        pps = arrs.get("DSP_12", np.array([]))
        if len(pps) and not np.all(np.isnan(pps)):
            ax_s = fig_time.axes[2]
            ax_s.fill_between(t_full, pps * 60, step="post", color=GREEN, alpha=0.3, label="PPS")
            ax_s.legend(fontsize=7, facecolor=BG_CARD, edgecolor="none", labelcolor=FG_TEXT)
        self._fig_time = fig_time
        self._embed_figure(fig_time, self._tab_time)


def _style_axes(ax, tiny=False):
    fs = 6 if tiny else 7
    ax.tick_params(colors=FG_MUTED, labelsize=fs)
    for side in ("top", "right"):
        ax.spines[side].set_visible(False)
    for side in ("left", "bottom"):
        ax.spines[side].set_color(FG_MUTED)
    ax.grid(True, color="#45475a", linewidth=0.4, linestyle="--")


# ==============================================================================
# Entry point
# ==============================================================================
if __name__ == "__main__":
    app = FDRConverterApp()
    app.mainloop()
