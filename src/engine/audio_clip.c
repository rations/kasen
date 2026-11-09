#include <string.h>
#include <sndfile.h>
#include "audio_clip.h"

AudioClip *audio_clip_new(const gchar *path, GError **err)
{
    SF_INFO info = {0};
    SNDFILE *sf = sf_open(path, SFM_READ, &info);
    if (!sf) {
        g_set_error(err, G_FILE_ERROR, G_FILE_ERROR_FAILED, "%s: %s", path, sf_strerror(NULL));
        return NULL;
    }

    if (info.frames <= 0 || info.channels <= 0 || info.samplerate <= 0) {
        sf_close(sf);
        g_set_error(err, G_FILE_ERROR, G_FILE_ERROR_FAILED, "%s: invalid audio parameters", path);
        return NULL;
    }

    AudioClip *clip = g_new0(AudioClip, 1);
    clip->path = g_strdup(path);
    clip->info = info;
    clip->refcount = 1;

    int ch = info.channels;
    clip->n_blocks = (guint32)((info.frames + AUDIO_CLIP_PEAK_BLOCK - 1) / AUDIO_CLIP_PEAK_BLOCK);
    clip->block_peaks = g_new(gfloat, clip->n_blocks * ch * 2);

    gfloat *buf = g_new(gfloat, AUDIO_CLIP_PEAK_BLOCK * ch);

    for (guint32 b = 0; b < clip->n_blocks; b++) {
        sf_count_t n = sf_readf_float(sf, buf, AUDIO_CLIP_PEAK_BLOCK);
        if (n <= 0) {
            for (guint32 bb = b; bb < clip->n_blocks; bb++)
                for (int c = 0; c < ch; c++) {
                    clip->block_peaks[(bb * ch + c) * 2] = 0.0f;
                    clip->block_peaks[(bb * ch + c) * 2 + 1] = 0.0f;
                }
            break;
        }
        for (int c = 0; c < ch; c++) {
            gfloat mn = 1.0f, mx = -1.0f;
            for (sf_count_t s = 0; s < n; s++) {
                gfloat v = buf[s * ch + c];
                if (v < mn)
                    mn = v;
                if (v > mx)
                    mx = v;
            }
            clip->block_peaks[(b * ch + c) * 2] = mn;
            clip->block_peaks[(b * ch + c) * 2 + 1] = mx;
        }
    }

    g_free(buf);
    sf_close(sf);

    /* Coarser levels, each folded from the one below. */
    const gfloat *src = clip->block_peaks;
    guint32 src_n = clip->n_blocks;
    for (int lv = 0; lv < AUDIO_CLIP_PEAK_LEVELS && src_n > AUDIO_CLIP_PEAK_FANOUT; lv++) {
        guint32 dst_n = (src_n + AUDIO_CLIP_PEAK_FANOUT - 1) / AUDIO_CLIP_PEAK_FANOUT;
        gfloat *dst = g_new(gfloat, (gsize)dst_n * ch * 2);
        for (guint32 d = 0; d < dst_n; d++) {
            guint32 s0 = d * AUDIO_CLIP_PEAK_FANOUT;
            guint32 s1 = MIN(s0 + AUDIO_CLIP_PEAK_FANOUT, src_n);
            for (int c = 0; c < ch; c++) {
                gfloat mn = 1.0f, mx = -1.0f;
                for (guint32 b = s0; b < s1; b++) {
                    mn = MIN(mn, src[(b * ch + c) * 2]);
                    mx = MAX(mx, src[(b * ch + c) * 2 + 1]);
                }
                dst[(d * ch + c) * 2] = mn;
                dst[(d * ch + c) * 2 + 1] = mx;
            }
        }
        clip->mip_peaks[lv] = dst;
        clip->mip_blocks[lv] = dst_n;
        src = dst;
        src_n = dst_n;
    }
    return clip;
}

AudioClip *audio_clip_ref(AudioClip *clip)
{
    if (!clip)
        return NULL;
    g_atomic_int_inc(&clip->refcount);
    return clip;
}

void audio_clip_free(AudioClip *clip)
{
    if (!clip)
        return;
    if (!g_atomic_int_dec_and_test(&clip->refcount))
        return; /* still referenced elsewhere */
    g_free(clip->block_peaks);
    for (int lv = 0; lv < AUDIO_CLIP_PEAK_LEVELS; lv++)
        g_free(clip->mip_peaks[lv]);
    g_free(clip->path);
    g_free(clip);
}

void audio_peak_table_query(const gfloat *table, guint32 n_blocks, int ch, int block,
                            sf_count_t start, sf_count_t end, gint xres, gfloat *out_min,
                            gfloat *out_max)
{
    if (xres <= 0 || ch <= 0)
        return;
    if (!table || n_blocks == 0 || block <= 0 || end <= start) {
        for (gint i = 0; i < xres * ch; i++) {
            out_min[i] = 0.0f;
            out_max[i] = -1.0f;
        }
        return;
    }

    gdouble spp = (gdouble)(end - start) / (gdouble)xres;

    for (gint x = 0; x < xres; x++) {
        sf_count_t s0 = start + (sf_count_t)(x * spp);
        sf_count_t s1 = start + (sf_count_t)((x + 1) * spp);
        if (s1 <= s0)
            s1 = s0 + 1;
        if (s1 > end)
            s1 = end;

        guint32 b0 = s0 < 0 ? 0 : (guint32)(s0 / block);
        guint32 b1 = s1 <= 0 ? 0 : (guint32)((s1 - 1) / block) + 1;
        if (b1 > n_blocks)
            b1 = n_blocks;

        for (int c = 0; c < ch; c++) {
            gfloat mn = 1.0f, mx = -1.0f;
            for (guint32 b = b0; b < b1; b++) {
                gfloat bmin = table[((gsize)b * ch + c) * 2];
                gfloat bmax = table[((gsize)b * ch + c) * 2 + 1];
                if (bmin < mn)
                    mn = bmin;
                if (bmax > mx)
                    mx = bmax;
            }
            out_min[x * ch + c] = mn;
            out_max[x * ch + c] = mx;
        }
    }
}

void audio_clip_get_peaks(AudioClip *clip, sf_count_t start, sf_count_t end, gint xres,
                          gfloat *out_min, gfloat *out_max)
{
    if (!clip) {
        for (gint i = 0; i < xres; i++) {
            if (out_min)
                out_min[i] = 0.0f;
            if (out_max)
                out_max[i] = -1.0f;
        }
        return;
    }
    if (!out_min || !out_max)
        return;

    /* The coarsest level whose entries still fit inside one pixel. */
    const gfloat *table = clip->block_peaks;
    guint32 n = clip->n_blocks;
    int block = AUDIO_CLIP_PEAK_BLOCK;
    gdouble spp = xres > 0 ? (gdouble)(end - start) / (gdouble)xres : 0.0;
    for (int lv = 0; lv < AUDIO_CLIP_PEAK_LEVELS; lv++) {
        int lv_block = block * AUDIO_CLIP_PEAK_FANOUT;
        if (!clip->mip_peaks[lv] || spp < (gdouble)lv_block)
            break;
        table = clip->mip_peaks[lv];
        n = clip->mip_blocks[lv];
        block = lv_block;
    }
    audio_peak_table_query(table, n, clip->info.channels, block, start, end, xres, out_min,
                           out_max);
}

sf_count_t audio_clip_read_frames(AudioClip *clip, sf_count_t start_frame, sf_count_t n_frames,
                                  gfloat *out)
{
    if (!clip || n_frames <= 0)
        return 0;

    SF_INFO info = {0};
    SNDFILE *sf = sf_open(clip->path, SFM_READ, &info);
    if (!sf)
        return 0;

    sf_seek(sf, start_frame, SEEK_SET);
    sf_count_t got = sf_readf_float(sf, out, n_frames);
    sf_close(sf);
    return got;
}
