#define WINVER 0x0500                                  /* �o�[�W������` Windows2000�ȏ� */
#define DIRECTSOUND_VERSION 0x0900

#ifndef _WIN32_WINNT
#define _WIN32_WINNT WINVER
#endif

#include "ruby.h"
#ifndef RUBY_ST_H
#include "st.h"
#endif

#include <dmusici.h>

#define DXRUBY_EXTERN 1
#include "dxruby.h"
#include "sound.h"

#include <mfapi.h>
#include <mfplay.h>
#include <mfreadwrite.h>

// ---
// stb_vorbis - Ogg Vorbis audio decoder - public domain/MIT licensed
// Originally written by Sean T. Barrett (https://nothings.org/)
// https://github.com/nothings/stb
//
// This file includes stb_vorbis as an inline implementation (.inc) file
// and is used in accordance with its public domain / MIT license terms.

#include "stb_vorbis.c.inc"

// ---

#ifndef DS3DALG_DEFAULT
GUID DS3DALG_DEFAULT = {0};
#endif

#define WAVE_RECT 0
#define WAVE_SIN 1
#define WAVE_SAW 2
#define WAVE_TRI 3

static VALUE cSoundEffect;  /* �������ʉ��N���X     */
static VALUE cSound;        /* �T�E���h�N���X       */

static LPDIRECTSOUND8 g_pDSound = NULL;   /* DirectSound�C���^�[�t�F�C�X */
static int            g_iRefDS  = 0;      /* DirectSound�̎Q�ƃJ�E���g   */

/* Sound�N���X�̍\���� */
struct DXRubySound {
    LPDIRECTSOUNDBUFFER pDSBuffer;  /* DirectSound�o�b�t�@        */
    int start;                      /* �J�n�ʒu(���g�p)           */
    int loopstart;                  /* ���[�v�J�n�ʒu(���g�p)     */
    int loopend;                    /* ���[�v�I���ʒu(���g�p)     */
    int loopcount;                  /* ���[�v��                 */
    int midwavflag;                 /* wav:1, mp3:2, ogg:3        */
    VALUE vbuffer;                  /* Ruby�̃o�b�t�@             */
};

/* SoundEffect�I�u�W�F�N�g */
struct DXRubySoundEffect {
    LPDIRECTSOUNDBUFFER pDSBuffer;  /* �o�b�t�@                   */
};


/*********************************************************************
 * Sound�N���X
 *
 * �y���C�T�v�z2025/05
 *    �{��DXRuby�Ŏg�p���Ă��� DirectMusic��
 *    Windows10/11 64bit���ł͎����I�Ɏg�p�ł��Ȃ����߁A
 *    Sound�N���X�ł� SoundEffect�N���X�Ɠ��l��
 *    DirectSound���g�p����
 *    .WAV�t�@�C������� .MP3�t�@�C�����Đ��ł���悤�ɉ��C
 *
 * �yDirectMusic���g�p�����ꍇ�̃G���[��z
 *    'DXRuby::Sound#initialize': DirectMusic initialize error - CoCreateInstance (DXRuby::DXRubyError)
 *    HRESULT = 0x80040154 (COM�R���|�[�l���g�����W�X�g���ɓo�^����Ă��Ȃ�)
 *********************************************************************/


/*--------------------------------------------------------------------
   �������̉���������Ȃ��A���\�[�X�����
 ---------------------------------------------------------------------*/
static void Sound_free( struct DXRubySound *sound )
{
    /* �T�E���h�o�b�t�@���J�� */
    RELEASE( sound->pDSBuffer );

    g_iRefDS--;   /* SoundEffect�N���X�Ƌ��ʂ̎Q�ƃJ�E���^���g�p */

    if( g_iRefDS <= 0 )
    {
        RELEASE( g_pDSound );
    }
}

/*--------------------------------------------------------------------
   �K�[�x�W�R���N�^���Ώۂ̃I�u�W�F�N�g��ǐՂł���悤�ɂ���
 ---------------------------------------------------------------------*/
static void Sound_mark( struct DXRubySound *sound )
{
    rb_gc_mark( sound->vbuffer );
}

/*--------------------------------------------------------------------
   �K�[�x�W�R���N�^���Ώۂ̃I�u�W�F�N�g���������ۂɌĂ΂��֐�
 ---------------------------------------------------------------------*/
static void Sound_release( struct DXRubySound *sound )
{
    if( sound->pDSBuffer )
    {
        Sound_free( sound );
    }
    free( sound );
    sound = NULL;

    g_iRefAll--;
    if( g_iRefAll == 0 )
    {
        CoUninitialize();
    }
}

#ifdef DXRUBY_USE_TYPEDDATA
const rb_data_type_t Sound_data_type = {
    "Sound",
    {
    Sound_mark,
    Sound_release,
    0,
    },
    NULL, NULL
};
#endif

/*--------------------------------------------------------------------
   �������m�� (initialize�O�ɌĂ΂��)
 ---------------------------------------------------------------------*/
static VALUE Sound_allocate( VALUE klass )
{
    VALUE obj;
    struct DXRubySound *sound;

    /* DXRubySound�̃������m�� */
    sound = malloc(sizeof(struct DXRubySound));
    if( sound == NULL ) rb_raise( eDXRubyError, "Out of memory - Sound_allocate" );
#ifdef DXRUBY_USE_TYPEDDATA
    obj = TypedData_Wrap_Struct( klass, &Sound_data_type, sound );
#else
    obj = Data_Wrap_Struct(klass, 0, Sound_release, sound);
#endif

    sound->pDSBuffer = NULL;

    return obj;
}

/*--------------------------------------------------------------------
   �I�u�W�F�N�g�̔j��
 ---------------------------------------------------------------------*/
static VALUE Sound_dispose( VALUE self )
{
    struct DXRubySound *sound = DXRUBY_GET_STRUCT( Sound, self );
    DXRUBY_CHECK_DISPOSE( sound, pDSBuffer );
    Sound_free( sound );
    return self;
}

/*--------------------------------------------------------------------
   �I�u�W�F�N�g�j���󋵂̃`�F�b�N
 ---------------------------------------------------------------------*/
static VALUE Sound_check_disposed( VALUE self )
{
    if( DXRUBY_GET_STRUCT( Sound, self )->pDSBuffer == NULL )
    {
        return Qtrue;
    }

    return Qfalse;
}

/*--------------------------------------------------------------------
   �w�肳�ꂽ�t�@�C�������� IMFSourceReader���쐬
 ---------------------------------------------------------------------*/
static IMFSourceReader *create_source_reader(const WCHAR *filename) {
    IMFSourceReader *reader = NULL;
    IMFAttributes   *attr   = NULL;
    HRESULT          hr;

    hr = MFCreateAttributes(&attr, 1);
    if (FAILED(hr)) return NULL;

    hr = attr->lpVtbl->SetUINT32(attr, &MF_READWRITE_DISABLE_CONVERTERS, FALSE);
    if (FAILED(hr)) {
        attr->lpVtbl->Release(attr);
        return NULL;
    }

    hr = MFCreateSourceReaderFromURL(filename, attr, &reader);
    attr->lpVtbl->Release(attr);

    return SUCCEEDED(hr) ? reader : NULL;
}

/*--------------------------------------------------------------------
   IMFSourceReader���� MP3������ǂݎ��APCM �f�[�^�Ƃ��ăf�R�[�h
 ---------------------------------------------------------------------*/
void decode_mp3(IMFSourceReader *reader, BYTE **pcm_data, DWORD *pcm_size, WAVEFORMATEX **wf) {
    IMFMediaType   *mediaType  = NULL;
    IMFMediaBuffer *buffer     = NULL;
    IMFSample      *sample     = NULL;
    DWORD           total_size = 0;
    DWORD           cap        = 1024 * 1024;
    HRESULT         hr;

    // �o�̓t�H�[�}�b�g�� PCM �Ɏw��
    MFCreateMediaType(&mediaType);
    mediaType->lpVtbl->SetGUID(mediaType, &MF_MT_MAJOR_TYPE, &MFMediaType_Audio);
    mediaType->lpVtbl->SetGUID(mediaType, &MF_MT_SUBTYPE, &MFAudioFormat_PCM);
    reader->lpVtbl->SetCurrentMediaType(reader, (DWORD)MF_SOURCE_READER_FIRST_AUDIO_STREAM, NULL, mediaType);
    mediaType->lpVtbl->Release(mediaType);

    // �o�̓t�H�[�}�b�g�擾
    IMFMediaType *outType = NULL;
    reader->lpVtbl->GetCurrentMediaType(reader, MF_SOURCE_READER_FIRST_AUDIO_STREAM, &outType);
    UINT32 cbSize = 0;
    MFCreateWaveFormatExFromMFMediaType(outType, wf, &cbSize, 0);   // �g�p��� CoTaskMemFree()�ɂ�� wf�̃�����������K�v
    outType->lpVtbl->Release(outType);

    *pcm_data = malloc(cap);
    if (!*pcm_data) {
        CoTaskMemFree(wf);
        rb_raise(eDXRubyError, "Not enough memory for `pcm_data`");
    }

    while (1) {
        DWORD flags = 0;
        LONGLONG ts;
        hr = reader->lpVtbl->ReadSample(reader, MF_SOURCE_READER_FIRST_AUDIO_STREAM, 0, NULL, &flags, &ts, &sample);
        if (FAILED(hr) || flags & MF_SOURCE_READERF_ENDOFSTREAM) break;
        if (!sample) continue;

        sample->lpVtbl->ConvertToContiguousBuffer(sample, &buffer);
        BYTE *data = NULL;
        DWORD len = 0;
        buffer->lpVtbl->Lock(buffer, &data, NULL, &len);

        if (total_size + len > cap) {
            cap *= 2;
            *pcm_data = realloc(*pcm_data, cap);
        }
        memcpy(*pcm_data + total_size, data, len);
        total_size += len;

        buffer->lpVtbl->Unlock(buffer);
        buffer->lpVtbl->Release(buffer);
        sample->lpVtbl->Release(sample);
    }
    *pcm_size = total_size;
}

/*--------------------------------------------------------------------
   MP3�t�@�C����ǂݍ��݁APCM�f�[�^�Ƃ��ăf�R�[�h
 ---------------------------------------------------------------------*/
void decode_mp3_file(const char *filename_utf8, BYTE **pcm_data, DWORD *pcm_size, WAVEFORMATEX **wf) {
    WCHAR   wfilename[MAX_PATH];
    HRESULT hr;

    // Media Foundation ������
    hr = MFStartup(MF_VERSION, MFSTARTUP_LITE);
    if (FAILED(hr)) {
        rb_raise(eDXRubyError, "MFStartup failed (0x%lx)", hr);
    }

    // MP3�f�R�[�h�p�� SourceReader �쐬
    MultiByteToWideChar(CP_UTF8, 0, filename_utf8, -1, wfilename, MAX_PATH);
    IMFSourceReader *reader = create_source_reader(wfilename);
    if (!reader) {
        rb_raise(eDXRubyError, "Failed to open MP3 file `%s`", filename_utf8);
    }

    // MP3�f�R�[�h
    decode_mp3(reader, pcm_data, pcm_size, wf);
    if (!*pcm_data) {
        rb_raise(eDXRubyError, "Failed to decode MP3");
    }

    reader->lpVtbl->Release(reader);
    MFShutdown();
}

/*--------------------------------------------------------------------
   OGG�t�@�C����ǂݍ��݁APCM�f�[�^�Ƃ��ăf�R�[�h
 ---------------------------------------------------------------------*/
void decode_ogg_file(const char *filename_utf8, BYTE **pcm_data, DWORD *pcm_size, WAVEFORMATEX **wf) {
    WCHAR wfilename[MAX_PATH];
    *pcm_data = NULL;
    *pcm_size = 0;
    *wf       = NULL;

    MultiByteToWideChar(CP_UTF8, 0, filename_utf8, -1, wfilename, MAX_PATH);
    FILE *fp = _wfopen(wfilename, L"rb");
    if (!fp) {
        rb_raise(eDXRubyError, "Failed to open OGG file `%s`", filename_utf8);
    }

    int error;
    stb_vorbis *v = stb_vorbis_open_file(fp, 0, &error, NULL);
    if (!v) {
        fclose(fp);
        rb_raise(eDXRubyError, "Failed to decode OGG stream: `%s` (error=%d)", filename_utf8, error);
    }

    stb_vorbis_info info = stb_vorbis_get_info(v);
    int samples = stb_vorbis_stream_length_in_samples(v);
    int channels = info.channels;

    short *output = (short *)malloc(samples * channels * sizeof(short));
    if (!output) {
        stb_vorbis_close(v);
        fclose(fp);
        rb_raise(eDXRubyError, "Out of memory for decoded PCM buffer: `%s`", filename_utf8);
    }

    int actual_samples = stb_vorbis_get_samples_short_interleaved(v, channels, output, samples * channels);
    stb_vorbis_close(v);
    fclose(fp);
    if (actual_samples <= 0) {
        free(output);
        rb_raise(eDXRubyError, "OGG decoding produced no samples: `%s`", filename_utf8);
    }

    *pcm_data = (BYTE *)output;
    *pcm_size = actual_samples * channels * sizeof(short);

    WAVEFORMATEX *format = (WAVEFORMATEX *)malloc(sizeof(WAVEFORMATEX));
    if (!format) {
        free(output);
        rb_raise(eDXRubyError, "Out of memory for WAVEFORMATEX: `%s`", filename_utf8);
    }
    format->wFormatTag      = WAVE_FORMAT_PCM;
    format->nChannels       = channels;
    format->nSamplesPerSec  = info.sample_rate;
    format->wBitsPerSample  = 16;
    format->nBlockAlign     = channels * 2;
    format->nAvgBytesPerSec = format->nSamplesPerSec * format->nBlockAlign;
    format->cbSize          = 0;

    *wf = format;
}

/*--------------------------------------------------------------------
   WAV�t�@�C����ǂݍ��݁APCM�f�[�^�Ƃ��ăf�R�[�h
 ---------------------------------------------------------------------*/
void decode_wav_file(const char *filename_utf8, BYTE **pcm_data, DWORD *pcm_size, WAVEFORMATEX **wf) {
   /*
    * .wav�t�@�C���̍\��
    * 
    *   [0x00] "RIFF"             (4�o�C�g) �Œ蕶����
    *   [0x04] <�t�@�C���T�C�Y>   (4�o�C�g) ���̌�̃t�@�C���T�C�Y
    *   [0x08] "WAVE"             (4�o�C�g) �Œ蕶����
    *   
    *   ---- �`�����N�J�n ----
    *   [0x0C] "fmt "             (4�o�C�g) �t�H�[�}�b�g�`�����N��ID
    *   [0x10] <�`�����N�T�C�Y>   (4�o�C�g) �ʏ��16�܂���18�Ȃ�
    *   [0x14] <�t�H�[�}�b�g�f�[�^>�iWAVEFORMATEX�����j
    *   
    *   ...���̑��`�����N�i���Ƃ��� "fact" �`�����N�Ȃǁj...
    *   
    *   [??]   "data"             (4�o�C�g) �T�E���h�f�[�^�`�����N��ID
    *   [??]   <�`�����N�T�C�Y>   (4�o�C�g)
    *   [??]   <PCM�f�[�^�{��>
    *
    */

    WCHAR wfilename[MAX_PATH];
    BYTE *data      = NULL;
    DWORD data_size =  0;
    int   pos       = 12;

    MultiByteToWideChar(CP_UTF8, 0, filename_utf8, -1, wfilename, MAX_PATH);
    FILE *fp = _wfopen(wfilename, L"rb");
    if (!fp) {
        rb_raise(eDXRubyError, "Failed to open WAV file `%s`", filename_utf8);
    }

    fseek(fp, 0, SEEK_END);
    long size = ftell(fp);

    BYTE *wav_file_buffer = (BYTE *)malloc(size);
    if (!wav_file_buffer) {
        fclose(fp);
        rb_raise(eDXRubyError, "Memory allocation failed");
    }

    fseek(fp, 0, SEEK_SET);
    fread(wav_file_buffer, 1, size, fp);
    fclose(fp);

    if (memcmp(wav_file_buffer, "RIFF", 4) != 0 || memcmp(wav_file_buffer + 8, "WAVE", 4) != 0) {
        free(wav_file_buffer);
        rb_raise(eDXRubyError, "Not a valid WAV file");
    }

    *wf = (WAVEFORMATEX *)malloc(sizeof(WAVEFORMATEX));
    if (!*wf) {
        free(wav_file_buffer);
        rb_raise(eDXRubyError, "Not enough memory for `wf`");
    }

    while (pos + 8 <= size) {
        DWORD chunkSize = *(DWORD *)(wav_file_buffer + pos + 4);

        // chunkSize �̑Ó������`�F�b�N
        if (pos + 8 + chunkSize > size) {
            free(*wf);
            free(wav_file_buffer);
            rb_raise(eDXRubyError, "Corrupted WAV file (chunk overflow)");
        }

        if (memcmp(wav_file_buffer + pos, "fmt ", 4) == 0) {
            // "fmt " �`�����N���� WAVEFORMATEX�\���̂̃t�H�[�}�b�g�����擾
            DWORD fmt_size = chunkSize;
            if (fmt_size < sizeof(WAVEFORMATEX)) {
                // WAVEFORMATEX�S�̂�0�ŏ��������Afmt�`�����N�ɂ���o�C�g�������R�s�[����
                memset(*wf, 0, sizeof(WAVEFORMATEX));
                memcpy(*wf, wav_file_buffer + pos + 8, fmt_size);
            } else {
                memcpy(*wf, wav_file_buffer + pos + 8, sizeof(WAVEFORMATEX));
            }
        } else if (memcmp(wav_file_buffer + pos, "data", 4) == 0) {
            // "data" �`�����N���� PCM�����f�[�^�{�̂Ƃ��̃T�C�Y���擾
            data = wav_file_buffer + pos + 8;
            data_size = chunkSize;
        }
        pos += 8 + ((chunkSize + 1) & ~1);
    }

    if (!data || data_size == 0) {
        free(*wf);
        free(wav_file_buffer);
        rb_raise(eDXRubyError, "No wave data found");
    }

    // pcm_data �ɒ��ڃ����������蓖��
    *pcm_data = (BYTE *)malloc(data_size);
    if (!*pcm_data) {
        free(*wf);
        free(wav_file_buffer);
        rb_raise(eDXRubyError, "Not enough memory for `pcm_data`");
    }

    // pcm_data �ɃR�s�[
    memcpy(*pcm_data, data, data_size);
    *pcm_size = data_size;
    free(wav_file_buffer); // wav_file_buffer �͂����ŉ��
}

/*--------------------------------------------------------------------
   �I�u�W�F�N�g����
 ---------------------------------------------------------------------*/
// �T�E���h������
static VALUE Sound_initialize(VALUE obj, VALUE vfilename) {
    struct DXRubySound *sound;                 // �T�E���h�N���X�̃C���X�^���X
    const char         *filename_utf8;         // �t�@�C����(.wav | .mp3)
    BYTE               *pcm_data = NULL;       // PCM�f�[�^
    DWORD               pcm_size = 0;          // PCM�f�[�^�̃T�C�Y
    WAVEFORMATEX       *wf       = NULL;       // �t�H�[�}�b�g
    HRESULT             hr;

    g_iRefAll++;
    // DirectSound ������
    if (g_iRefDS == 0) {
        hr = CoInitialize(NULL);
        if (FAILED(hr)) {
            rb_raise(eDXRubyError, "COM initialize failed - CoInitialize (0x%lx)", hr);
        }

        hr = DirectSoundCreate8(NULL, &g_pDSound, NULL);
        if (FAILED(hr)) {
            CoUninitialize();
            rb_raise(eDXRubyError, "DirectSound initialize failed - DirectSoundCreate8 (0x%lx)", hr);
        }

        hr = g_pDSound->lpVtbl->SetCooperativeLevel(g_pDSound, g_hWnd, DSSCL_PRIORITY);
        if (FAILED(hr)) {
            RELEASE(g_pDSound);
            CoUninitialize();
            rb_raise(eDXRubyError, "Set cooperative level failed (0x%lx)", hr);
        }
    }
    g_iRefDS++;

    // �\���̎擾�ƃN���[���A�b�v
    sound = DXRUBY_GET_STRUCT(Sound, obj);
    if (sound->pDSBuffer) {
        Sound_free(sound);
    }
    sound->vbuffer    = Qnil;
    sound->loopcount  = 0;

    // �t�@�C��������
    Check_Type(vfilename, T_STRING);
    // UTF-8 �ȊO�Ȃ� ArgumentError
    if (rb_enc_get_index(vfilename) != rb_utf8_encindex()) {
        rb_raise(rb_eArgError, "filename must be a UTF-8 encoded string");
    }
    // UTF-8 �� UTF-16�iWindows API�p�j�ɕϊ�
    filename_utf8 = RSTRING_PTR(vfilename);

    // �g���q�擾
    const char *ext = filename_utf8 + strlen(filename_utf8) - 4;

    // PCM�f�[�^�Ƃ��ăf�R�[�h
    if (_stricmp(ext, ".wav") == 0) {
        // WAV�t�@�C��
        decode_wav_file(filename_utf8, &pcm_data, &pcm_size, &wf);
        sound->midwavflag = 1;
    } else if (_stricmp(ext, ".mp3") == 0) {
        // MP3�t�@�C��
        decode_mp3_file(filename_utf8, &pcm_data, &pcm_size, &wf);
        sound->midwavflag = 2;
    } else if (_stricmp(ext, ".ogg") == 0) {
        // OGG�t�@�C��
        decode_ogg_file(filename_utf8, &pcm_data, &pcm_size, &wf);
        sound->midwavflag = 3;
    } else {
        rb_raise(eDXRubyError, "Unsupported audio format `%s`", filename_utf8);
    }

    // DirectSound �o�b�t�@�쐬
    DSBUFFERDESC desc = {0};
    desc.dwSize = sizeof(DSBUFFERDESC);
    desc.dwFlags = DSBCAPS_CTRLVOLUME;
    desc.dwBufferBytes = pcm_size;
    desc.lpwfxFormat = wf;

    IDirectSoundBuffer *pTmp = NULL;
    hr = g_pDSound->lpVtbl->CreateSoundBuffer(g_pDSound, &desc, &pTmp, NULL);
    if (FAILED(hr)) {
        free(pcm_data);
        rb_raise(eDXRubyError, "CreateSoundBuffer failed (0x%lx)", hr);
    }

    // �o�b�t�@�� PCM�f�[�^�����[�h
    VOID *p1, *p2;
    DWORD b1, b2;
    hr = pTmp->lpVtbl->Lock(pTmp, 0, pcm_size, &p1, &b1, &p2, &b2, 0);
    if (FAILED(hr)) {
        pTmp->lpVtbl->Release(pTmp);
        free(pcm_data);
        rb_raise(eDXRubyError, "Buffer lock failed (0x%lx)", hr);
    }

    // PCM�f�[�^���R�s�[
    memcpy(p1, pcm_data, b1);
    if (p2 && b2 > 0) memcpy(p2, pcm_data + b1, b2);
    pTmp->lpVtbl->Unlock(pTmp, p1, b1, p2, b2);

    // DXRubySound�̍\���̂փf�[�^��ݒ�
    sound->pDSBuffer =  pTmp;

    // ���������
    if (pcm_data) free(pcm_data);
    if (sound->midwavflag == 1) {          // .wav
        if (wf) free(wf);
    } else if (sound->midwavflag == 2) {   // .mp3
        CoTaskMemFree(wf);
    } else if (sound->midwavflag == 3) {   // .ogg
        if (wf) free(wf);
    }

    return obj;
}

/*--------------------------------------------------------------------
   �Đ�
 ---------------------------------------------------------------------*/
static VALUE Sound_play(VALUE obj) {
    struct DXRubySound *sound;
    HRESULT hr;

    sound = DXRUBY_GET_STRUCT(Sound, obj);

    if (!sound->pDSBuffer) return Qnil;

    // �Đ��ʒu��擪�ɖ߂�
    sound->pDSBuffer->lpVtbl->SetCurrentPosition(sound->pDSBuffer, 0);

    // loopcount �� DSBPLAY_LOOPING �� 0 �łȂ���΁A�G���[
    if (sound->loopcount != DSBPLAY_LOOPING && sound->loopcount != 0) {
      rb_raise(rb_eRuntimeError, "Invalid loop count value. Must be DSBPLAY_LOOPING or 0.");
    }

    // �Đ�
    hr = sound->pDSBuffer->lpVtbl->Play(sound->pDSBuffer, 0, 0, sound->loopcount);

    if (FAILED(hr)) {
      rb_raise(rb_eRuntimeError, "Failed to play sound");
    }

    return Qnil;
}

/*--------------------------------------------------------------------
   ��~
 ---------------------------------------------------------------------*/
static VALUE Sound_stop(VALUE obj) {
    struct DXRubySound *sound;
    sound = DXRUBY_GET_STRUCT(Sound, obj);

    if (!sound->pDSBuffer) return Qnil;

    sound->pDSBuffer->lpVtbl->Stop(sound->pDSBuffer);
    return Qnil;
}

/*--------------------------------------------------------------------
   ���ʐݒ�

   ��DirectSound�ł� -10000(DSBVOLUME_MIN)�`0(�ő�)
   ���������A�ΐ��X�P�[���̂��ߎ��ۂɂ� -5000���x�łقږ����ɂȂ�
   ��DXRuby������ 0�`255�� -5000�`0�Ƀ}�b�s���O�����A
     0�̂Ƃ��̂݊��S�~���[�g�� -10000�Ƃ���
 ---------------------------------------------------------------------*/
static VALUE Sound_setVolume(VALUE obj, VALUE vvolume) {
    struct DXRubySound *sound;
    LONG dsVolume;
    int volume;

    sound = DXRUBY_GET_STRUCT(Sound, obj);
    if (!sound->pDSBuffer) return Qnil;

    DXRUBY_CHECK_DISPOSE(sound, pDSBuffer);

    volume = NUM2INT(vvolume);
    if (volume <   0) volume =   0;
    if (volume > 255) volume = 255;

    if (volume == 0) {
        dsVolume = -10000;  // ���S�~���[�g
    } else {
        dsVolume = (LONG)((volume * 5000.0f / 255.0f) - 5000.0f);  // -5000 �` 0
    }

    sound->pDSBuffer->lpVtbl->SetVolume(sound->pDSBuffer, dsVolume);

    return obj;
}

/*--------------------------------------------------------------------
   ���ʎ擾
 ---------------------------------------------------------------------*/
static VALUE Sound_getVolume(VALUE obj) {
    struct DXRubySound *sound = DXRUBY_GET_STRUCT(Sound, obj);
    LONG dsVolume;

    if (!sound->pDSBuffer) return INT2FIX(255); // �o�b�t�@�����������Ȃ�ő剹��

    if (FAILED(IDirectSoundBuffer_GetVolume(sound->pDSBuffer, &dsVolume))) {
        rb_raise(eDXRubyError, "Failed to get volume.");
    }

    // DirectSound�� -5000�`0 �� Ruby�� 0�`255 �ɕϊ�
    int rubyVolume = (int)(((dsVolume + -5000.0f) * 255.0f / -5000.0f) + 0.5f);
    if (rubyVolume < 0) rubyVolume = 0;
    if (rubyVolume > 255) rubyVolume = 255;

    if (dsVolume <= -10000 + 100) {
        rubyVolume = 0;  // ���S�~���[�g����
    } else {
        rubyVolume = (int)(((dsVolume + 5000.0f) * 255.0f / 5000.0f) + 0.5f);  // -5000�`0 �� 0�`255
    }
    // fprintf(stderr, "%d %ld -> %d\n", DSBVOLUME_MIN, dsVolume, rubyVolume);

    return INT2FIX(rubyVolume);
}

/*--------------------------------------------------------------------
   ���[�v�񐔂̐ݒ�

   ��DirectMusic�ł͔C�ӂ̉񐔎w�肪�ł������A
     DirectSound�ł͂P��݂̂̍Đ��܂��͖������[�v�̂����ꂩ

   ���{��DXRuby�̎d�l�𓥏P��������
     -1 �͖������[�v�A����ȊO�̐��l�͂��ׂ� 0 (���s�񐔂P��)
 ---------------------------------------------------------------------*/
static VALUE Sound_setLoopCount(VALUE obj, VALUE vloopcount)
{
    struct DXRubySound *sound;

    sound = DXRUBY_GET_STRUCT(Sound, obj);
    DXRUBY_CHECK_DISPOSE(sound, pDSBuffer);

    sound->loopcount = NUM2INT(vloopcount) == -1 ? DSBPLAY_LOOPING : 0;

    return obj;
}

/*--------------------------------------------------------------------
   ���[�v�̔ۂ̐ݒ�

   ��DirectMusic�ł͔C�ӂ̉񐔎w�肪�ł������A
     DirectSound�ł͂P��݂̂̍Đ��܂��͖������[�v�̂����ꂩ

   �������I�� boolean(true/false)�Ŏw��ł���悤�ɂ�������
 ---------------------------------------------------------------------*/
static VALUE Sound_loop(VALUE obj, VALUE flag)
{
    struct DXRubySound *sound;
    sound = DXRUBY_GET_STRUCT(Sound, obj);
    DXRUBY_CHECK_DISPOSE(sound, pDSBuffer);

    if (flag != Qtrue && flag != Qfalse) {
        rb_raise(rb_eTypeError, "expected boolean (true or false)");
    }

    sound->loopcount = flag == Qtrue ? DSBPLAY_LOOPING : 0;

    return obj;
}



/*********************************************************************
 * SoundEffect�N���X
 *
 * DirectSound���g�p���ĉ���炷�B
 * �Ƃ肠���������o�����Ɗ撣���Ă���B
 *********************************************************************/

/*--------------------------------------------------------------------
   �Q�Ƃ���Ȃ��Ȃ����Ƃ���GC����Ă΂��֐�
 ---------------------------------------------------------------------*/
static void SoundEffect_free( struct DXRubySoundEffect *soundeffect )
{
    /* �T�E���h�o�b�t�@���J�� */
    RELEASE( soundeffect->pDSBuffer );

    g_iRefDS--;

    if( g_iRefDS == 0 )
    {
        RELEASE( g_pDSound );
    }

}

static void SoundEffect_release( struct DXRubySoundEffect *soundeffect )
{
    if( soundeffect->pDSBuffer )
    {
        SoundEffect_free( soundeffect );
    }
    free( soundeffect );
    soundeffect = NULL;

    g_iRefAll--;
    if( g_iRefAll == 0 )
    {
        CoUninitialize();
    }
}

#ifdef DXRUBY_USE_TYPEDDATA
const rb_data_type_t SoundEffect_data_type = {
    "SoundEffect",
    {
    0,
    SoundEffect_release,
    0,
    },
    NULL, NULL
};
#endif

/*--------------------------------------------------------------------
   SoundEffect�N���X��dispose�B
 ---------------------------------------------------------------------*/
static VALUE SoundEffect_dispose( VALUE self )
{
    struct DXRubySoundEffect *soundeffect = DXRUBY_GET_STRUCT( SoundEffect, self );
    DXRUBY_CHECK_DISPOSE( soundeffect, pDSBuffer );
    SoundEffect_free( soundeffect );
    return self;
}

/*--------------------------------------------------------------------
   SoundEffect�N���X��disposed?�B
 ---------------------------------------------------------------------*/
static VALUE SoundEffect_check_disposed( VALUE self )
{
    if( DXRUBY_GET_STRUCT( SoundEffect, self )->pDSBuffer == NULL )
    {
        return Qtrue;
    }

    return Qfalse;
}

/*--------------------------------------------------------------------
   SoundEffect�N���X��allocate�B���������m�ۂ���ׂ�initialize�O�ɌĂ΂��B
 ---------------------------------------------------------------------*/
static VALUE SoundEffect_allocate( VALUE klass )
{
    VALUE obj;
    struct DXRubySoundEffect *soundeffect;

    /* DXRubySoundEffect�̃������擾��SoundEffect�I�u�W�F�N�g���� */
    soundeffect = malloc(sizeof(struct DXRubySoundEffect));
    if( soundeffect == NULL ) rb_raise( eDXRubyError, "Out of memory - SoundEffect_allocate" );
#ifdef DXRUBY_USE_TYPEDDATA
    obj = TypedData_Wrap_Struct( klass, &SoundEffect_data_type, soundeffect );
#else
    obj = Data_Wrap_Struct(klass, 0, SoundEffect_release, soundeffect);
#endif

    /* �Ƃ肠�����T�E���h�I�u�W�F�N�g��NULL�ɂ��Ă��� */
    soundeffect->pDSBuffer = NULL;

    return obj;
}


static short calcwave(int type, double vol, double count, double p, double duty)
{
	switch( type )
	{
	case 1: /* �T�C���g */
		return (short)((sin( (3.141592653589793115997963468544185161590576171875f * 2) * (double)count / (double)p )) * (double)vol * 128);
		break;
	case 2: /* �m�R�M���g */
		return (short)(((double)count / (double)p - 0.5) * (double)vol * 256);
		break;
	case 3: /* �O�p�g */
		if( count < p / 4 )			/* 1/4 */
		{
			return (short)((double)count / ((double)p / 4) * (double)vol * 128);
		}
		else if( count < p / 2 )		/* 2/4 */
		{
			return (short)(((double)p / 2 - (double)count) / ((double)p / 4) * (double)vol * 128);
		}
		else if( count < p * 3 / 4 )	/* 3/4 */
		{
			return (short)(-((double)count - (double)p / 2)/ ((double)p / 4) * (double)vol * 128);
		}
		else											/* �Ō� */
		{
			return (short)(-((double)p - (double)count) / ((double)p / 4) * (double)vol * 128);
		}
		break;
	case 0: /* ��`�g */
	default: /* �f�t�H���g */
		if( count < p * duty )	/* �O�� */
		{
			return (short)(vol * 128);
		}
		else									/* �㔼 */
		{
		    return (short)(-vol * 128);
		}
		break;
	}
}


/*--------------------------------------------------------------------
   SoundEffect�N���X��initialize�B�g�`�𐶐�����B
 ---------------------------------------------------------------------*/
static VALUE SoundEffect_initialize( int argc, VALUE *argv, VALUE obj )
{
    HRESULT hr;
    struct DXRubySoundEffect *soundeffect;
    DSBUFFERDESC desc;
    WAVEFORMATEX pcmwf;
    int i;
    short *pointer, *pointer2;
    DWORD size, size2;
    VALUE vf;
    double count, duty = 0.5;
    double vol;
    double f;
    VALUE vsize, vtype, vresolution;
    int type, resolution;

    g_iRefAll++;

    rb_scan_args( argc, argv, "12", &vsize, &vtype, &vresolution );

    type       = vtype       == Qnil ? 0    : NUM2INT( vtype );
    resolution = vresolution == Qnil ? 1000 : (NUM2INT( vresolution ) > 44100 ? 44100 : NUM2INT( vresolution ));

    /* DirectSound�I�u�W�F�N�g�̍쐬 */
    if( g_iRefDS == 0 )
    {
        hr = DirectSoundCreate8( &DSDEVID_DefaultPlayback, &g_pDSound, NULL );
        if( FAILED( hr ) )
        {
            rb_raise( eDXRubyError, "DirectSound initialize error - DirectSoundCreate8" );
        }

        hr = g_pDSound->lpVtbl->SetCooperativeLevel( g_pDSound, g_hWnd, DSSCL_PRIORITY );
        if( FAILED( hr ) )
        {
            rb_raise( eDXRubyError, "DirectSound initialize error - SetCooperativeLevel" );
        }
    }
    g_iRefDS++;

    /* �T�E���h�I�u�W�F�N�g�쐬 */
    soundeffect = DXRUBY_GET_STRUCT( SoundEffect, obj );
    if( soundeffect->pDSBuffer )
    {
        g_iRefDS++;
        SoundEffect_free( soundeffect );
        g_iRefDS--;
        g_iRefAll++;
    }

    /* �T�E���h�o�b�t�@�쐬 */
    pcmwf.wFormatTag = WAVE_FORMAT_PCM;
    pcmwf.nChannels = 1;
    pcmwf.nSamplesPerSec = 44100;
    pcmwf.wBitsPerSample = 16;
    pcmwf.nBlockAlign = pcmwf.nChannels * pcmwf.wBitsPerSample / 8;
    pcmwf.nAvgBytesPerSec = pcmwf.nSamplesPerSec * pcmwf.nBlockAlign;
    pcmwf.cbSize = 0;

    desc.dwSize = sizeof(desc);
    desc.dwFlags = DSBCAPS_GLOBALFOCUS;
#ifdef DXRUBY15
    if( TYPE( vsize ) == T_ARRAY )
    {
        desc.dwBufferBytes = RARRAY_LEN( vsize ) * (pcmwf.nChannels * pcmwf.wBitsPerSample / 8);
    }
    else
    {
#endif
        desc.dwBufferBytes = pcmwf.nAvgBytesPerSec / 100 * NUM2INT(vsize) / 10;
#ifdef DXRUBY15
    }
#endif
    desc.dwReserved = 0;
    desc.lpwfxFormat = &pcmwf;
    desc.guid3DAlgorithm = DS3DALG_DEFAULT;

    hr = g_pDSound->lpVtbl->CreateSoundBuffer( g_pDSound, &desc, &soundeffect->pDSBuffer, NULL );

    if( FAILED( hr ) )
    {
        rb_raise( eDXRubyError, "Failure to create the SoundBuffer - CreateSoundBuffer" );
    }

    /* ���b�N */
    hr = soundeffect->pDSBuffer->lpVtbl->Lock( soundeffect->pDSBuffer, 0, 0, (VOID**)&pointer, &size, (VOID**)&pointer2, &size2, DSBLOCK_ENTIREBUFFER );
    if( FAILED( hr ) || size2 != 0 )
    {
        rb_raise( eDXRubyError, "Failure to lock the SoundBuffer - Lock" );
    }

    if( TYPE( vsize ) == T_ARRAY )
    {
        /* ���o�b�t�@���� */
        for( i = 0; i < desc.dwBufferBytes / (pcmwf.wBitsPerSample / 8); i++ )
        {
            double tmp = NUM2DBL( RARRAY_AREF( vsize, i ) );
            if( tmp < 0.0 )
            {
                if( tmp < -1.0 ) tmp = -1.0;
                pointer[i] = (short)(tmp * 32768.0);
            }
            else
            {
                if( tmp > 1.0 ) tmp = 1.0;
                pointer[i] = (short)(tmp * 32767.0);
            }
        }
    }
    else
    {
        /* ���o�b�t�@������ */
        for( i = 0; i < desc.dwBufferBytes / (pcmwf.wBitsPerSample / 8); i++ )
        {
            pointer[i] = 0;
        }

        count = 0;

        /* �g�`���� */
        for( i = 0; i < desc.dwBufferBytes / (pcmwf.wBitsPerSample / 8); i++ )
        {
            /* �w�莞�ԒP�ʂŃu���b�N���Ăяo�� */
            if ( i % (pcmwf.nSamplesPerSec / resolution) == 0 )
            {
                vf = rb_yield( obj );
                if( TYPE( vf ) != T_ARRAY )
                {
                    soundeffect->pDSBuffer->lpVtbl->Unlock( soundeffect->pDSBuffer, pointer, size, pointer2, size2 );
                    rb_raise(rb_eTypeError, "invalid value - SoundEffect_initialize");
                    break;
                }
                f = NUM2DBL( rb_ary_entry(vf, 0) );
                vol = NUM2DBL(rb_ary_entry(vf, 1));
                if( RARRAY_LEN( vf ) > 2 )
                {
                    duty = NUM2DBL(rb_ary_entry(vf, 2));
                }
                /* �ő�/�Œ���g���ƍő�{�����[���̐��� */
                f = f > pcmwf.nSamplesPerSec / 2.0f ? pcmwf.nSamplesPerSec / 2.0f : f;
                f = f < 20 ? 20 : f;
                vol = vol > 255 ? 255 : vol;
                vol = vol < 0 ? 0 : vol;
            }
            count = count + f;
            if( count >= pcmwf.nSamplesPerSec )
            {
                count = count - pcmwf.nSamplesPerSec;
            }
            pointer[i] = calcwave(type, vol, count, pcmwf.nSamplesPerSec, duty);
        }
    }

    /* �A�����b�N */
    hr = soundeffect->pDSBuffer->lpVtbl->Unlock( soundeffect->pDSBuffer, pointer, size, pointer2, size2 );
    if( FAILED( hr ) )
    {
        rb_raise( eDXRubyError, "Failure to lock the SoundBuffer - Lock" );
    }

    return obj;

}


/*--------------------------------------------------------------------
   �g�`����������B
 ---------------------------------------------------------------------*/
static VALUE SoundEffect_add( int argc, VALUE *argv, VALUE obj )
{
    HRESULT hr;
    struct DXRubySoundEffect *soundeffect;
	DSBUFFERDESC desc;
	WAVEFORMATEX pcmwf;
	int i;
	short *pointer, *pointer2;
	DWORD size, size2;
	VALUE vf, vtype, vresolution;
	double count, duty = 0.5;
	double vol;
    double f;
	int type, resolution;
	int data;

	rb_scan_args( argc, argv, "02", &vtype, &vresolution );

	type       = vtype       == Qnil ? 0    : NUM2INT( vtype );
	resolution = vresolution == Qnil ? 1000 : (NUM2INT( vresolution ) > 44100 ? 44100 : NUM2INT( vresolution ));

    /* �T�E���h�I�u�W�F�N�g�擾 */
    soundeffect = DXRUBY_GET_STRUCT( SoundEffect, obj );
    DXRUBY_CHECK_DISPOSE( soundeffect, pDSBuffer );

	/* ���b�N */
	hr = soundeffect->pDSBuffer->lpVtbl->Lock( soundeffect->pDSBuffer, 0, 0, (VOID**)&pointer, &size, (VOID**)&pointer2, &size2, DSBLOCK_ENTIREBUFFER );
    if( FAILED( hr ) || size2 != 0 )
    {
        rb_raise( eDXRubyError, "Failure to lock the SoundBuffer - Lock" );
    }

	pcmwf.nChannels = 1;
	pcmwf.nSamplesPerSec = 44100;
	pcmwf.wBitsPerSample = 16;
	pcmwf.nBlockAlign = pcmwf.nChannels * pcmwf.wBitsPerSample / 8;
	pcmwf.nAvgBytesPerSec = pcmwf.nSamplesPerSec * pcmwf.nBlockAlign;
	desc.dwBufferBytes = size;

	count = 0;

	/* �g�`���� */
	for( i = 0; i < desc.dwBufferBytes / (pcmwf.wBitsPerSample / 8); i++ )
	{
		/* �w�莞�ԒP�ʂŃu���b�N���Ăяo�� */
		if ( i % (pcmwf.nSamplesPerSec / resolution) == 0 )
		{
			vf = rb_yield( obj );
			if( TYPE( vf ) != T_ARRAY )
			{
				soundeffect->pDSBuffer->lpVtbl->Unlock( soundeffect->pDSBuffer, pointer, size, pointer2, size2 );
			    rb_raise(rb_eTypeError, "invalid value - SoundEffect_add");
				break;
			}
			f = NUM2DBL( rb_ary_entry(vf, 0) );
			vol = NUM2DBL(rb_ary_entry(vf, 1));
            if( RARRAY_LEN( vf ) > 2 )
            {
                duty = NUM2DBL(rb_ary_entry(vf, 2));
            }
			/* �ő�/�Œ���g���ƍő�{�����[���̐��� */
			f = f > pcmwf.nSamplesPerSec / 2.0f ? pcmwf.nSamplesPerSec / 2.0f : f;
			f = f < 20 ? 20 : f;
			vol = vol > 255 ? 255 : vol;
			vol = vol < 0 ? 0 : vol;
		}
		count = count + f;
		if( count >= pcmwf.nSamplesPerSec )
		{
			count = count - pcmwf.nSamplesPerSec;
		}

		data = calcwave(type, vol, count, pcmwf.nSamplesPerSec, duty);

		if( data + (int)pointer[i] > 32767 )
		{
			pointer[i] = 32767;
		}
		else if( data + (int)pointer[i] < -32768 )
		{
			pointer[i] = -32768;
		}
		else
		{
			pointer[i] += data;
		}
	}

	/* �A�����b�N */
	hr = soundeffect->pDSBuffer->lpVtbl->Unlock( soundeffect->pDSBuffer, pointer, size, pointer2, size2 );
    if( FAILED( hr ) )
    {
        rb_raise( eDXRubyError, "Failure to lock the SoundBuffer - Lock" );
    }

    return obj;

}


/*--------------------------------------------------------------------
   ����炷
 ---------------------------------------------------------------------*/
static VALUE SoundEffect_play( int argc, VALUE *argv, VALUE self )
{
    HRESULT hr;
    struct DXRubySoundEffect *se = DXRUBY_GET_STRUCT( SoundEffect, self );
    VALUE vflag;
    DXRUBY_CHECK_DISPOSE( se, pDSBuffer );

    rb_scan_args( argc, argv, "01", &vflag );

    /* �Ƃ߂� */
    hr = se->pDSBuffer->lpVtbl->Stop( se->pDSBuffer );
    if( FAILED( hr ) )
    {
        rb_raise( eDXRubyError, "Sound play failed - SoundEffect_play" );
    }

    hr = se->pDSBuffer->lpVtbl->SetCurrentPosition( se->pDSBuffer, 0 );
    if( FAILED( hr ) )
    {
        rb_raise( eDXRubyError, "Sound play failed - SoundEffect_play" );
    }

    /* �Đ� */
    hr = se->pDSBuffer->lpVtbl->Play( se->pDSBuffer, 0, 0, RTEST(vflag) ? DSBPLAY_LOOPING : 0 );
    if( FAILED( hr ) )
    {
        rb_raise( eDXRubyError, "Sound play failed - SoundEffect_play" );
    }
    return self;
}


/*--------------------------------------------------------------------
   �����~�߂�
 ---------------------------------------------------------------------*/
static VALUE SoundEffect_stop( VALUE obj )
{
    HRESULT hr;
    struct DXRubySoundEffect *soundeffect;

    soundeffect = DXRUBY_GET_STRUCT( SoundEffect, obj );
    DXRUBY_CHECK_DISPOSE( soundeffect, pDSBuffer );

    /* �Ƃ߂� */
    hr = soundeffect->pDSBuffer->lpVtbl->Stop( soundeffect->pDSBuffer );
    if( FAILED( hr ) )
    {
        rb_raise( eDXRubyError, "���̒�~�Ɏ��s���܂��� - Stop" );
    }

    return obj;
}


/*--------------------------------------------------------------------
   wav�t�@�C���o��
 ---------------------------------------------------------------------*/
static VALUE SoundEffect_save( VALUE self, VALUE vfilename )
{
    HRESULT hr;
    HANDLE hfile;
    short *pointer, *pointer2;
    DWORD size, size2;
    DWORD tmpl;
    WORD tmps;
    DWORD wsize;
    struct DXRubySoundEffect *se = DXRUBY_GET_STRUCT( SoundEffect, self );

    DXRUBY_CHECK_DISPOSE( se, pDSBuffer );

    /* ���b�N */
    hr = se->pDSBuffer->lpVtbl->Lock( se->pDSBuffer, 0, 0, (VOID**)&pointer, &size, (VOID**)&pointer2, &size2, DSBLOCK_ENTIREBUFFER );
    if( FAILED( hr ) || size2 != 0 )
    {
        rb_raise( eDXRubyError, "Failure to lock the SoundBuffer - Lock" );
    }

    hfile = CreateFile( RSTRING_PTR( vfilename ), GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    if( hfile == INVALID_HANDLE_VALUE )
    {
        rb_raise( eDXRubyError, "Write failure - open" );
    }

    if( !WriteFile( hfile, "RIFF", 4, &wsize, NULL ) ) rb_raise( eDXRubyError, "Write failure - write" );
    tmpl = size + 44 - 8;
    if( !WriteFile( hfile, &tmpl, 4, &wsize, NULL ) ) rb_raise( eDXRubyError, "Write failure - write" );
    if( !WriteFile( hfile, "WAVE", 4, &wsize, NULL ) ) rb_raise( eDXRubyError, "Write failure - write" );
    if( !WriteFile( hfile, "fmt ", 4, &wsize, NULL ) ) rb_raise( eDXRubyError, "Write failure - write" );
    tmpl = 16;
    if( !WriteFile( hfile, &tmpl, 4, &wsize, NULL ) ) rb_raise( eDXRubyError, "Write failure - write" );
    tmps = 1;
    if( !WriteFile( hfile, &tmps, 2, &wsize, NULL ) ) rb_raise( eDXRubyError, "Write failure - write" );
    tmps = 1;
    if( !WriteFile( hfile, &tmps, 2, &wsize, NULL ) ) rb_raise( eDXRubyError, "Write failure - write" );
    tmpl = 44100;
    if( !WriteFile( hfile, &tmpl, 4, &wsize, NULL ) ) rb_raise( eDXRubyError, "Write failure - write" );
    tmpl = 44100*2;
    if( !WriteFile( hfile, &tmpl, 4, &wsize, NULL ) ) rb_raise( eDXRubyError, "Write failure - write" );
    tmps = 2;
    if( !WriteFile( hfile, &tmps, 2, &wsize, NULL ) ) rb_raise( eDXRubyError, "Write failure - write" );
    tmps = 16;
    if( !WriteFile( hfile, &tmps, 2, &wsize, NULL ) ) rb_raise( eDXRubyError, "Write failure - write" );

    if( !WriteFile( hfile, "data", 4, &wsize, NULL ) ) rb_raise( eDXRubyError, "Write failure - write" );
    tmpl = size;
    if( !WriteFile( hfile, &tmpl, 4, &wsize, NULL ) ) rb_raise( eDXRubyError, "Write failure - write" );
    if( !WriteFile( hfile, pointer, size, &wsize, NULL ) ) rb_raise( eDXRubyError, "Write failure - write" );

    CloseHandle( hfile );

    /* �A�����b�N */
    hr = se->pDSBuffer->lpVtbl->Unlock( se->pDSBuffer, pointer, size, pointer2, size2 );
    if( FAILED( hr ) )
    {
        rb_raise( eDXRubyError, "Failure to lock the SoundBuffer - Lock" );
    }

    return self;
}


/*--------------------------------------------------------------------
   �z��
 ---------------------------------------------------------------------*/
static VALUE SoundEffect_to_a( VALUE self )
{
    HRESULT hr;
    short *pointer, *pointer2;
    DWORD size, size2;
    struct DXRubySoundEffect *se = DXRUBY_GET_STRUCT( SoundEffect, self );
    VALUE ary;
    int i;

    DXRUBY_CHECK_DISPOSE( se, pDSBuffer );

    /* ���b�N */
    hr = se->pDSBuffer->lpVtbl->Lock( se->pDSBuffer, 0, 0, (VOID**)&pointer, &size, (VOID**)&pointer2, &size2, DSBLOCK_ENTIREBUFFER );
    if( FAILED( hr ) || size2 != 0 )
    {
        rb_raise( eDXRubyError, "Failure to lock the SoundBuffer - Lock" );
    }

    ary = rb_ary_new2( size / 2 );
    for( i = 0; i < size / 2; i++ )
    {
        double tmp;
        if( pointer[i] < 0 )
        {
            tmp = pointer[i] / 32768.0;
        }
        else
        {
            tmp = pointer[i] / 32767.0;
        }
        rb_ary_push( ary, rb_float_new( tmp ) );
    }

    /* �A�����b�N */
    hr = se->pDSBuffer->lpVtbl->Unlock( se->pDSBuffer, pointer, size, pointer2, size2 );
    if( FAILED( hr ) )
    {
        rb_raise( eDXRubyError, "Failure to lock the SoundBuffer - Lock" );
    }

    return ary;
}


void Init_dxruby_Sound( void )
{

    /* Sound�N���X��` */
    cSound = rb_define_class_under( mDXRuby, "Sound", rb_cObject );

    /* Sound�N���X�Ƀ��\�b�h�o�^*/
    rb_define_private_method( cSound, "initialize"   , (VALUE (*)(ANYARGS))Sound_initialize      , 1 );
    rb_define_method( cSound, "dispose"              , (VALUE (*)(ANYARGS))Sound_dispose         , 0 );
    rb_define_method( cSound, "disposed?"            , (VALUE (*)(ANYARGS))Sound_check_disposed  , 0 );
    rb_define_method( cSound, "play"                 , (VALUE (*)(ANYARGS))Sound_play            , 0 );
    rb_define_method( cSound, "stop"                 , (VALUE (*)(ANYARGS))Sound_stop            , 0 );
    rb_define_method( cSound, "volume"               , (VALUE (*)(ANYARGS))Sound_getVolume       , 0 );
    rb_define_method( cSound, "volume="              , (VALUE (*)(ANYARGS))Sound_setVolume       , 1 );
    rb_define_alias(  cSound, "set_volume", "volume=");
    rb_define_method( cSound, "loop_count="          , (VALUE (*)(ANYARGS))Sound_setLoopCount    , 1 );
    rb_define_method( cSound, "loop="                , (VALUE (*)(ANYARGS))Sound_loop            , 1 );

    /* Sound�I�u�W�F�N�g�𐶐���������initialize�̑O�ɌĂ΂�郁�������蓖�Ċ֐��o�^ */
    rb_define_alloc_func( cSound, Sound_allocate );


    /* SoundEffect�N���X��` */
    cSoundEffect = rb_define_class_under( mDXRuby, "SoundEffect", rb_cObject );

    /* SoundEffect�N���X�Ƀ��\�b�h�o�^*/
    rb_define_private_method( cSoundEffect, "initialize", (VALUE (*)(ANYARGS))SoundEffect_initialize, -1 );
    rb_define_method( cSoundEffect, "dispose"   , (VALUE (*)(ANYARGS))SoundEffect_dispose   , 0 );
    rb_define_method( cSoundEffect, "disposed?" , (VALUE (*)(ANYARGS))SoundEffect_check_disposed, 0 );
    rb_define_method( cSoundEffect, "play"      , (VALUE (*)(ANYARGS))SoundEffect_play      , -1 );
    rb_define_method( cSoundEffect, "stop"      , (VALUE (*)(ANYARGS))SoundEffect_stop      , 0 );
    rb_define_method( cSoundEffect, "add"       , (VALUE (*)(ANYARGS))SoundEffect_add       , -1 );
    rb_define_method( cSoundEffect, "save"      , (VALUE (*)(ANYARGS))SoundEffect_save      , 1 );
#ifdef DXRUBY15
    rb_define_method( cSoundEffect, "to_a"      , (VALUE (*)(ANYARGS))SoundEffect_to_a      , 0 );
#endif
    /* SoundEffect�I�u�W�F�N�g�𐶐���������initialize�̑O�ɌĂ΂�郁�������蓖�Ċ֐��o�^ */
    rb_define_alloc_func( cSoundEffect, SoundEffect_allocate );

    rb_define_const( mDXRuby, "WAVE_SIN"     , INT2FIX(WAVE_SIN) );
    rb_define_const( mDXRuby, "WAVE_SAW"     , INT2FIX(WAVE_SAW) );
    rb_define_const( mDXRuby, "WAVE_TRI"     , INT2FIX(WAVE_TRI) );
    rb_define_const( mDXRuby, "WAVE_RECT"    , INT2FIX(WAVE_RECT) );

    rb_define_const( mDXRuby, "TYPE_MIDI"    , INT2FIX(0) );
    rb_define_const( mDXRuby, "TYPE_WAV"     , INT2FIX(1) );
}

