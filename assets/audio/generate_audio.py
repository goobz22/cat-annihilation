#!/usr/bin/env python3
"""
Audio Generator for Cat Annihilation Game
Generates all required sound effects and music using numpy
"""

import numpy as np
import struct
import os

# Constants
SAMPLE_RATE = 44100
BIT_DEPTH = 16

def write_wav(filename, audio_data, sample_rate=SAMPLE_RATE):
    """Write audio data to WAV file using pure numpy (no scipy dependency)"""
    audio_data = np.clip(audio_data, -1.0, 1.0)
    audio_data = (audio_data * 32767).astype(np.int16)

    num_samples = len(audio_data)
    num_channels = 1
    byte_rate = sample_rate * num_channels * 2
    block_align = num_channels * 2

    with open(filename, 'wb') as f:
        # RIFF header
        f.write(b'RIFF')
        f.write(struct.pack('<I', 36 + num_samples * 2))
        f.write(b'WAVE')

        # fmt chunk
        f.write(b'fmt ')
        f.write(struct.pack('<I', 16))  # Chunk size
        f.write(struct.pack('<H', 1))   # Audio format (PCM)
        f.write(struct.pack('<H', num_channels))
        f.write(struct.pack('<I', sample_rate))
        f.write(struct.pack('<I', byte_rate))
        f.write(struct.pack('<H', block_align))
        f.write(struct.pack('<H', BIT_DEPTH))

        # data chunk
        f.write(b'data')
        f.write(struct.pack('<I', num_samples * 2))
        f.write(audio_data.tobytes())

def apply_envelope(audio, attack=0.01, decay=0.1, sustain=0.7, release=0.2):
    """Apply ADSR envelope to audio"""
    length = len(audio)
    sr = SAMPLE_RATE

    attack_samples = int(attack * sr)
    decay_samples = int(decay * sr)
    release_samples = int(release * sr)
    sustain_samples = max(0, length - attack_samples - decay_samples - release_samples)

    envelope = np.ones(length)

    # Attack
    if attack_samples > 0:
        envelope[:attack_samples] = np.linspace(0, 1, attack_samples)

    # Decay
    if decay_samples > 0:
        decay_start = attack_samples
        decay_end = attack_samples + decay_samples
        envelope[decay_start:decay_end] = np.linspace(1, sustain, decay_samples)

    # Sustain
    if sustain_samples > 0:
        sustain_start = attack_samples + decay_samples
        sustain_end = sustain_start + sustain_samples
        envelope[sustain_start:sustain_end] = sustain

    # Release
    if release_samples > 0:
        envelope[-release_samples:] = np.linspace(sustain, 0, release_samples)

    return audio * envelope

def lowpass_filter(audio, cutoff_freq, sample_rate=SAMPLE_RATE):
    """Simple lowpass filter"""
    RC = 1.0 / (cutoff_freq * 2 * np.pi)
    dt = 1.0 / sample_rate
    alpha = dt / (RC + dt)

    filtered = np.zeros_like(audio)
    filtered[0] = audio[0]
    for i in range(1, len(audio)):
        filtered[i] = filtered[i-1] + alpha * (audio[i] - filtered[i-1])

    return filtered

# ============================================================================
# SOUND EFFECTS
# ============================================================================

def generate_sword_swing():
    """Whoosh sound with falling pitch"""
    duration = 0.3
    t = np.linspace(0, duration, int(SAMPLE_RATE * duration))

    # Falling pitch whoosh
    freq = 800 * np.exp(-t * 10)
    wave = np.sin(2 * np.pi * freq * t)

    # Add noise for air movement
    noise = np.random.randn(len(t)) * 0.15
    wave += noise

    # Sharp attack, quick decay
    envelope = np.exp(-t * 8)
    wave *= envelope

    # Normalize
    wave = wave / np.max(np.abs(wave)) * 0.8

    return wave

def generate_sword_hit():
    """Impact thud sound"""
    duration = 0.2
    t = np.linspace(0, duration, int(SAMPLE_RATE * duration))

    # Low frequency impact
    freq = 120 * np.exp(-t * 15)
    wave = np.sin(2 * np.pi * freq * t)

    # Add sharp noise burst
    noise = np.random.randn(len(t)) * 0.4
    noise *= np.exp(-t * 25)

    wave = wave * 0.6 + noise * 0.4

    # Very sharp decay
    envelope = np.exp(-t * 12)
    wave *= envelope

    wave = wave / np.max(np.abs(wave)) * 0.9

    return wave

def generate_projectile_fire():
    """Magic casting sound"""
    duration = 0.4
    t = np.linspace(0, duration, int(SAMPLE_RATE * duration))

    # Rising magical tone
    freq = 400 + 600 * t / duration
    wave = np.sin(2 * np.pi * freq * t)

    # Add harmonics
    wave += 0.3 * np.sin(2 * np.pi * freq * 2 * t)
    wave += 0.15 * np.sin(2 * np.pi * freq * 3 * t)

    # Shimmer with modulation
    mod = 1 + 0.2 * np.sin(2 * np.pi * 12 * t)
    wave *= mod

    # Envelope
    wave = apply_envelope(wave, attack=0.05, decay=0.1, sustain=0.6, release=0.15)

    wave = wave / np.max(np.abs(wave)) * 0.7

    return wave

def generate_projectile_hit():
    """Impact/explosion sound"""
    duration = 0.3
    t = np.linspace(0, duration, int(SAMPLE_RATE * duration))

    # Explosive noise burst
    noise = np.random.randn(len(t))

    # Filter to emphasize mid-low frequencies
    noise = lowpass_filter(noise, 2000)

    # Add low rumble
    rumble = np.sin(2 * np.pi * 60 * t)

    wave = noise * 0.7 + rumble * 0.3

    # Sharp attack, medium decay
    envelope = np.exp(-t * 10)
    wave *= envelope

    wave = wave / np.max(np.abs(wave)) * 0.85

    return wave

def generate_enemy_hurt():
    """Dog yelp sound"""
    duration = 0.3
    t = np.linspace(0, duration, int(SAMPLE_RATE * duration))

    # Sharp high-pitched yelp
    freq = 800 + 400 * np.sin(2 * np.pi * 8 * t)
    wave = np.sin(2 * np.pi * freq * t)

    # Add some growl
    wave += 0.2 * np.sin(2 * np.pi * freq * 0.5 * t)

    # Add noise for realism
    wave += np.random.randn(len(t)) * 0.1

    # Sharp envelope
    envelope = np.exp(-t * 15) * (1 + 0.3 * np.sin(2 * np.pi * 10 * t))
    wave *= envelope

    wave = wave / np.max(np.abs(wave)) * 0.75

    return wave

def generate_enemy_death():
    """Death sound - longer, more dramatic"""
    duration = 0.5
    t = np.linspace(0, duration, int(SAMPLE_RATE * duration))

    # Falling pitch with warble
    freq = 600 * np.exp(-t * 3) + 50 * np.sin(2 * np.pi * 6 * t)
    wave = np.sin(2 * np.pi * freq * t)

    # Add lower harmonic for depth
    wave += 0.4 * np.sin(2 * np.pi * freq * 0.5 * t)

    # Add noise
    wave += np.random.randn(len(t)) * 0.15 * np.exp(-t * 5)

    # Gradual decay
    envelope = np.exp(-t * 4)
    wave *= envelope

    wave = wave / np.max(np.abs(wave)) * 0.8

    return wave

def generate_player_hurt():
    """Cat hiss/pain sound"""
    duration = 0.3
    t = np.linspace(0, duration, int(SAMPLE_RATE * duration))

    # Hiss - mostly noise with some tone
    noise = np.random.randn(len(t))
    noise = lowpass_filter(noise, 4000)

    # High-pitched meow tone
    freq = 1000 + 200 * np.sin(2 * np.pi * 10 * t)
    tone = np.sin(2 * np.pi * freq * t)

    wave = noise * 0.6 + tone * 0.4

    # Sharp attack and decay
    envelope = np.exp(-t * 12)
    wave *= envelope

    wave = wave / np.max(np.abs(wave)) * 0.7

    return wave

def generate_player_death():
    """Sad meow sound"""
    duration = 0.8
    t = np.linspace(0, duration, int(SAMPLE_RATE * duration))

    # Meow with falling pitch
    freq = 800 - 300 * (t / duration)
    wave = np.sin(2 * np.pi * freq * t)

    # Add harmonics
    wave += 0.3 * np.sin(2 * np.pi * freq * 2 * t)
    wave += 0.15 * np.sin(2 * np.pi * freq * 1.5 * t)

    # Add slight vibrato
    vibrato = 1 + 0.05 * np.sin(2 * np.pi * 5 * t)
    wave *= vibrato

    # Sad envelope - slow fade
    wave = apply_envelope(wave, attack=0.1, decay=0.2, sustain=0.5, release=0.3)

    wave = wave / np.max(np.abs(wave)) * 0.75

    return wave

def generate_footstep():
    """Soft footstep sound"""
    duration = 0.1
    t = np.linspace(0, duration, int(SAMPLE_RATE * duration))

    # Low thud
    freq = 80 * np.exp(-t * 30)
    wave = np.sin(2 * np.pi * freq * t)

    # Add soft noise
    noise = np.random.randn(len(t)) * 0.3
    noise = lowpass_filter(noise, 500)

    wave = wave * 0.5 + noise * 0.5

    # Very sharp envelope
    envelope = np.exp(-t * 40)
    wave *= envelope

    wave = wave / np.max(np.abs(wave)) * 0.5

    return wave

def generate_jump():
    """Jump sound - quick rising tone"""
    duration = 0.2
    t = np.linspace(0, duration, int(SAMPLE_RATE * duration))

    # Rising pitch
    freq = 200 + 400 * (t / duration)
    wave = np.sin(2 * np.pi * freq * t)

    # Add some air noise
    noise = np.random.randn(len(t)) * 0.1
    wave += noise

    # Quick fade
    envelope = 1 - (t / duration)
    wave *= envelope

    wave = wave / np.max(np.abs(wave)) * 0.6

    return wave

def generate_land():
    """Landing thud"""
    duration = 0.15
    t = np.linspace(0, duration, int(SAMPLE_RATE * duration))

    # Low impact
    freq = 100 * np.exp(-t * 25)
    wave = np.sin(2 * np.pi * freq * t)

    # Add noise burst
    noise = np.random.randn(len(t)) * 0.4
    noise = lowpass_filter(noise, 800)

    wave = wave * 0.6 + noise * 0.4

    # Sharp decay
    envelope = np.exp(-t * 20)
    wave *= envelope

    wave = wave / np.max(np.abs(wave)) * 0.7

    return wave

def generate_pickup():
    """Coin/item pickup chime"""
    duration = 0.3
    t = np.linspace(0, duration, int(SAMPLE_RATE * duration))

    # Pleasant ascending arpeggio
    freq1 = 523.25  # C5
    freq2 = 659.25  # E5
    freq3 = 783.99  # G5

    # Three quick notes
    wave = np.zeros_like(t)
    third = len(t) // 3

    wave[:third] = np.sin(2 * np.pi * freq1 * t[:third])
    wave[third:2*third] = np.sin(2 * np.pi * freq2 * t[third:2*third])
    wave[2*third:] = np.sin(2 * np.pi * freq3 * t[2*third:])

    # Smooth envelope
    wave = apply_envelope(wave, attack=0.01, decay=0.05, sustain=0.7, release=0.15)

    wave = wave / np.max(np.abs(wave)) * 0.6

    return wave

def generate_wave_complete():
    """Victory fanfare"""
    duration = 1.0
    t = np.linspace(0, duration, int(SAMPLE_RATE * duration))

    # Victory melody: C - E - G - C (higher)
    notes = [
        (261.63, 0.0, 0.25),   # C4
        (329.63, 0.25, 0.5),   # E4
        (392.00, 0.5, 0.75),   # G4
        (523.25, 0.75, 1.0),   # C5
    ]

    wave = np.zeros_like(t)

    for freq, start, end in notes:
        start_idx = int(start * SAMPLE_RATE)
        end_idx = int(end * SAMPLE_RATE)
        t_note = t[start_idx:end_idx] - start

        # Main tone with harmonics
        note = np.sin(2 * np.pi * freq * t_note)
        note += 0.3 * np.sin(2 * np.pi * freq * 2 * t_note)
        note += 0.15 * np.sin(2 * np.pi * freq * 3 * t_note)

        # Note envelope
        note_env = np.exp(-t_note * 8)
        note *= note_env

        wave[start_idx:end_idx] += note

    # Overall envelope
    wave = apply_envelope(wave, attack=0.01, decay=0.1, sustain=0.8, release=0.2)

    wave = wave / np.max(np.abs(wave)) * 0.8

    return wave

def generate_menu_click():
    """UI click sound"""
    duration = 0.1
    t = np.linspace(0, duration, int(SAMPLE_RATE * duration))

    # Sharp click
    freq = 1200
    wave = np.sin(2 * np.pi * freq * t)

    # Very sharp envelope
    envelope = np.exp(-t * 50)
    wave *= envelope

    wave = wave / np.max(np.abs(wave)) * 0.5

    return wave

def generate_menu_hover():
    """UI hover sound - subtle"""
    duration = 0.05
    t = np.linspace(0, duration, int(SAMPLE_RATE * duration))

    # Soft tone
    freq = 800
    wave = np.sin(2 * np.pi * freq * t)

    # Quick fade
    envelope = np.exp(-t * 40)
    wave *= envelope

    wave = wave / np.max(np.abs(wave)) * 0.3

    return wave

# ============================================================================
# MUSIC
# ============================================================================

def generate_menu_music():
    """Calm menu loop - 10 seconds"""
    duration = 10.0
    t = np.linspace(0, duration, int(SAMPLE_RATE * duration))

    # Calm chord progression: Am - F - C - G
    # Using arpeggios

    wave = np.zeros_like(t)

    # Base tempo
    bpm = 80
    beat_duration = 60.0 / bpm

    # Am chord (A, C, E)
    freqs_am = [220.00, 261.63, 329.63]
    # F chord (F, A, C)
    freqs_f = [174.61, 220.00, 261.63]
    # C chord (C, E, G)
    freqs_c = [130.81, 164.81, 196.00]
    # G chord (G, B, D)
    freqs_g = [196.00, 246.94, 293.66]

    chords = [
        (freqs_am, 0, 2.5),
        (freqs_f, 2.5, 5.0),
        (freqs_c, 5.0, 7.5),
        (freqs_g, 7.5, 10.0),
    ]

    for freqs, start, end in chords:
        start_idx = int(start * SAMPLE_RATE)
        end_idx = int(end * SAMPLE_RATE)
        t_chord = t[start_idx:end_idx] - start

        chord_wave = np.zeros_like(t_chord)
        for freq in freqs:
            # Soft sine waves
            chord_wave += np.sin(2 * np.pi * freq * t_chord) / len(freqs)
            # Add subtle harmonics
            chord_wave += 0.1 * np.sin(2 * np.pi * freq * 2 * t_chord) / len(freqs)

        wave[start_idx:end_idx] += chord_wave

    # Fade in at start, fade out at end for looping
    fade_duration = 0.5
    fade_samples = int(fade_duration * SAMPLE_RATE)
    wave[:fade_samples] *= np.linspace(0, 1, fade_samples)
    wave[-fade_samples:] *= np.linspace(1, 0, fade_samples)

    wave = wave / np.max(np.abs(wave)) * 0.5

    return wave

def generate_gameplay_music():
    """Action loop - 15 seconds"""
    duration = 15.0
    t = np.linspace(0, duration, int(SAMPLE_RATE * duration))

    wave = np.zeros_like(t)

    # Faster tempo for action
    bpm = 140
    beat_duration = 60.0 / bpm

    # Action chord progression: Em - C - D - Em
    # Using power chords (root and fifth)

    chords = [
        ([164.81, 246.94], 0, 3.75),      # Em
        ([130.81, 196.00], 3.75, 7.5),    # C
        ([146.83, 220.00], 7.5, 11.25),   # D
        ([164.81, 246.94], 11.25, 15.0),  # Em
    ]

    for freqs, start, end in chords:
        start_idx = int(start * SAMPLE_RATE)
        end_idx = int(end * SAMPLE_RATE)
        t_chord = t[start_idx:end_idx] - start

        chord_wave = np.zeros_like(t_chord)
        for freq in freqs:
            # More aggressive tone
            chord_wave += np.sin(2 * np.pi * freq * t_chord) / len(freqs)
            # Add more harmonics for energy
            chord_wave += 0.2 * np.sin(2 * np.pi * freq * 2 * t_chord) / len(freqs)
            chord_wave += 0.1 * np.sin(2 * np.pi * freq * 3 * t_chord) / len(freqs)

        wave[start_idx:end_idx] += chord_wave

    # Add rhythmic element
    beat_freq = bpm / 60.0
    kick_pattern = np.sin(2 * np.pi * 60 * t) * (np.sin(2 * np.pi * beat_freq * t) > 0.9)
    wave += kick_pattern * 0.2

    # Fade in/out for looping
    fade_duration = 0.5
    fade_samples = int(fade_duration * SAMPLE_RATE)
    wave[:fade_samples] *= np.linspace(0, 1, fade_samples)
    wave[-fade_samples:] *= np.linspace(1, 0, fade_samples)

    wave = wave / np.max(np.abs(wave)) * 0.6

    return wave

def generate_victory_sting():
    """Victory music - 3 seconds"""
    duration = 3.0
    t = np.linspace(0, duration, int(SAMPLE_RATE * duration))

    wave = np.zeros_like(t)

    # Triumphant melody
    notes = [
        (523.25, 0.0, 0.4),    # C5
        (659.25, 0.4, 0.8),    # E5
        (783.99, 0.8, 1.2),    # G5
        (1046.50, 1.2, 2.0),   # C6
        (1046.50, 2.0, 3.0),   # C6 (held)
    ]

    for freq, start, end in notes:
        start_idx = int(start * SAMPLE_RATE)
        end_idx = int(end * SAMPLE_RATE)
        t_note = t[start_idx:end_idx] - start

        # Rich harmonics
        note = np.sin(2 * np.pi * freq * t_note)
        note += 0.4 * np.sin(2 * np.pi * freq * 2 * t_note)
        note += 0.2 * np.sin(2 * np.pi * freq * 3 * t_note)

        # Note envelope
        note_duration = end - start
        note_env = np.exp(-t_note / note_duration * 2)
        note *= note_env

        wave[start_idx:end_idx] += note

    # Overall envelope
    wave = apply_envelope(wave, attack=0.01, decay=0.2, sustain=0.7, release=0.5)

    wave = wave / np.max(np.abs(wave)) * 0.75

    return wave

def generate_defeat_sting():
    """Game over music - 3 seconds"""
    duration = 3.0
    t = np.linspace(0, duration, int(SAMPLE_RATE * duration))

    wave = np.zeros_like(t)

    # Sad descending melody
    notes = [
        (523.25, 0.0, 0.5),    # C5
        (493.88, 0.5, 1.0),    # B4
        (440.00, 1.0, 1.5),    # A4
        (392.00, 1.5, 2.0),    # G4
        (349.23, 2.0, 3.0),    # F4 (held)
    ]

    for freq, start, end in notes:
        start_idx = int(start * SAMPLE_RATE)
        end_idx = int(end * SAMPLE_RATE)
        t_note = t[start_idx:end_idx] - start

        # Somber tone
        note = np.sin(2 * np.pi * freq * t_note)
        note += 0.3 * np.sin(2 * np.pi * freq * 2 * t_note)

        # Slow decay
        note_duration = end - start
        note_env = np.exp(-t_note / note_duration)
        note *= note_env

        wave[start_idx:end_idx] += note

    # Overall envelope
    wave = apply_envelope(wave, attack=0.05, decay=0.3, sustain=0.6, release=0.8)

    wave = wave / np.max(np.abs(wave)) * 0.7

    return wave

# ============================================================================
# MAIN GENERATION
# ============================================================================

def main():
    """Generate all audio files"""

    script_dir = os.path.dirname(os.path.abspath(__file__))
    sfx_dir = os.path.join(script_dir, 'sfx')
    music_dir = os.path.join(script_dir, 'music')

    # Ensure directories exist
    os.makedirs(sfx_dir, exist_ok=True)
    os.makedirs(music_dir, exist_ok=True)

    print("Generating audio files for Cat Annihilation...")
    print("=" * 60)

    # Sound Effects
    sfx_files = [
        ('sword_swing.wav', generate_sword_swing),
        ('sword_hit.wav', generate_sword_hit),
        ('projectile_fire.wav', generate_projectile_fire),
        ('projectile_hit.wav', generate_projectile_hit),
        ('enemy_hurt.wav', generate_enemy_hurt),
        ('enemy_death.wav', generate_enemy_death),
        ('player_hurt.wav', generate_player_hurt),
        ('player_death.wav', generate_player_death),
        ('footstep.wav', generate_footstep),
        ('jump.wav', generate_jump),
        ('land.wav', generate_land),
        ('pickup.wav', generate_pickup),
        ('wave_complete.wav', generate_wave_complete),
        ('menu_click.wav', generate_menu_click),
        ('menu_hover.wav', generate_menu_hover),
    ]

    print("\nGenerating Sound Effects:")
    print("-" * 60)
    for filename, generator in sfx_files:
        filepath = os.path.join(sfx_dir, filename)
        audio = generator()
        write_wav(filepath, audio)
        print(f"✓ Generated: sfx/{filename}")

    # Music
    music_files = [
        ('menu_music.wav', generate_menu_music),
        ('gameplay_music.wav', generate_gameplay_music),
        ('victory_sting.wav', generate_victory_sting),
        ('defeat_sting.wav', generate_defeat_sting),
    ]

    print("\nGenerating Music:")
    print("-" * 60)
    for filename, generator in music_files:
        filepath = os.path.join(music_dir, filename)
        audio = generator()
        write_wav(filepath, audio)
        print(f"✓ Generated: music/{filename}")

    print("\n" + "=" * 60)
    print(f"✓ All audio files generated successfully!")
    print(f"  - {len(sfx_files)} sound effects in sfx/")
    print(f"  - {len(music_files)} music tracks in music/")
    print("=" * 60)

if __name__ == "__main__":
    main()
