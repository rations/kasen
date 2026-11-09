#ifndef AUDIO_CLIP_H_INCLUDED
#define AUDIO_CLIP_H_INCLUDED

#include <glib.h>
#include <sndfile.h>

G_BEGIN_DECLS

/* Frames per block-peak entry.  Smaller = finer resolution, more RAM. */
#define AUDIO_CLIP_PEAK_BLOCK 256
/* Coarser peak levels kept above the base table, and the step between them. */
#define AUDIO_CLIP_PEAK_LEVELS 2
#define AUDIO_CLIP_PEAK_FANOUT 16

/*
 * AudioClip — a reference to an audio file with a pre-computed peak table
 * for fast waveform rendering.
 *
 * The peak table stores min/max per channel per block of AUDIO_CLIP_PEAK_BLOCK
 * frames.  Layout:
 *   block_peaks[(b * channels + c) * 2 + 0] = min for block b, channel c
 *   block_peaks[(b * channels + c) * 2 + 1] = max for block b, channel c
 *
 * Values are normalised floats in [-1.0, 1.0].
 */
typedef struct {
    gchar *path;  /* file path (owned) */
    SF_INFO info; /* samplerate, channels, frames */

    gfloat *block_peaks; /* owned; length = n_blocks * channels * 2 */
    guint32 n_blocks;

    /* Coarser copies of block_peaks for zoomed-out drawing, same layout, each
     * AUDIO_CLIP_PEAK_FANOUT times coarser than the one before:
     * mip_peaks[0] = AUDIO_CLIP_PEAK_BLOCK * FANOUT frames per entry, [1] =
     * that * FANOUT. Owned; NULL when the clip is too short to need one. */
    gfloat *mip_peaks[AUDIO_CLIP_PEAK_LEVELS];
    guint32 mip_blocks[AUDIO_CLIP_PEAK_LEVELS];

    gint refcount; /* shared across clip regions; new = 1 */
} AudioClip;

/*
 * Load an audio file.  Reads the whole file once to build the peak table.
 * Returns NULL on failure; *err is set if err != NULL.  Refcount = 1.
 */
AudioClip *audio_clip_new(const gchar *path, GError **err);

/* Take an additional reference; returns the same clip (NULL-safe). */
AudioClip *audio_clip_ref(AudioClip *clip);

/* Drop a reference; frees the clip (and its buffers) when it reaches zero. */
void audio_clip_free(AudioClip *clip);

/*
 * Compute per-pixel peak data for waveform display.
 *
 *   start / end  — frame range to display (end exclusive)
 *   xres         — width of the display area in pixels
 *   out_min      — caller-allocated gfloat[xres * channels]
 *   out_max      — caller-allocated gfloat[xres * channels]
 *
 * On return, out_min[x*ch+c] <= out_max[x*ch+c] for pixels with data.
 * Pixels with no data have out_min > out_max.
 */
void audio_clip_get_peaks(AudioClip *clip, sf_count_t start, sf_count_t end, gint xres,
                          gfloat *out_min, gfloat *out_max);

/*
 * The query audio_clip_get_peaks runs, on any table in the block-peak layout
 * above: `table` has n_blocks entries of `channels` (min,max) pairs, each
 * covering `block` frames. Used directly for the live record waveform, whose
 * table grows while it is drawn (pass the published block count).
 * Same output contract as audio_clip_get_peaks (out arrays xres * channels).
 */
void audio_peak_table_query(const gfloat *table, guint32 n_blocks, int channels, int block,
                            sf_count_t start, sf_count_t end, gint xres, gfloat *out_min,
                            gfloat *out_max);

/*
 * Read interleaved float frames from the file.
 * Returns the number of frames actually read.
 */
sf_count_t audio_clip_read_frames(AudioClip *clip, sf_count_t start_frame, sf_count_t n_frames,
                                  gfloat *out);

G_END_DECLS

#endif /* AUDIO_CLIP_H_INCLUDED */
