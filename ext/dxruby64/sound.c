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
    int midwavflag;                 /* wav:1, mid: 0 (���g�p)     */
    VALUE vbuffer;                  /* Ruby�̃o�b�t�@             */
};

/* SoundEffect�I�u�W�F�N�g */
struct DXRubySoundEffect {
    LPDIRECTSOUNDBUFFER pDSBuffer;    /* �o�b�t�@         */
};


/*********************************************************************
 * Sound�N���X
 *
 * �y���C�T�v�z2025/05
 *    �{��DXRuby�Ŏg�p���Ă��� DirectMusic��
 *    Windows10/11 64bit���ł͎����I�Ɏg�p�ł��Ȃ����߁A
 *    Sound�N���X�ł� SoundEffect�N���X�Ɠ��l��
 *    DirectSound���g�p����悤���C�����B
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
   �I�u�W�F�N�g����
 ---------------------------------------------------------------------*/
static VALUE Sound_initialize(VALUE obj, VALUE vfilename)
{
    HRESULT hr;
    struct DXRubySound *sound;
    VALUE vsjisstr;
    CHAR *filename;
    FILE *fp = NULL;
    BYTE *data = NULL;
    DWORD size;
    DWORD pos = 0;

    g_iRefAll++;
    Check_Type(vfilename, T_STRING);

    // Shift_JIS �ϊ�
    if (rb_enc_get_index(vfilename) != 0) {
        vsjisstr = rb_str_export_to_enc(vfilename, g_enc_sys);
    } else {
        vsjisstr = vfilename;
    }

    filename = RSTRING_PTR(vsjisstr);

    // DirectSound ������
    if (g_iRefDS == 0) {
        hr = CoInitialize(NULL);
        if (FAILED(hr)) {
            rb_raise(eDXRubyError, "COM initialize failed - CoInitialize");
        }

        hr = DirectSoundCreate8(NULL, &g_pDSound, NULL);
        if (FAILED(hr)) {
            CoUninitialize();
            rb_raise(eDXRubyError, "DirectSound initialize failed - DirectSoundCreate8");
        }

        hr = g_pDSound->lpVtbl->SetCooperativeLevel(g_pDSound, g_hWnd, DSSCL_PRIORITY);
        if (FAILED(hr)) {
            RELEASE(g_pDSound);
            CoUninitialize();
            rb_raise(eDXRubyError, "Set cooperative level failed");
        }
    }
    g_iRefDS++;

    // �\���̎擾�ƃN���[���A�b�v
    sound = DXRUBY_GET_STRUCT(Sound, obj);
    if (sound->pDSBuffer) {
        Sound_free(sound);
    }

    // �t�@�C���ǂݍ���
    fp = fopen(filename, "rb");
    if (!fp) {
        rb_raise(eDXRubyError, "Failed to open file `%s`", filename);
    }
    fseek(fp, 0, SEEK_END);
    size = ftell(fp);
    fseek(fp, 0, SEEK_SET);
    data = malloc(size);
    if (!data) {
        fclose(fp);
        rb_raise(eDXRubyError, "Memory allocation failed");
    }
    fread(data, 1, size, fp);
    fclose(fp);

    // WAV �w�b�_���
    if (memcmp(data, "RIFF", 4) != 0 || memcmp(data + 8, "WAVE", 4) != 0) {
        free(data);
        rb_raise(eDXRubyError, "Not a valid WAV file");
    }

    pos = 12;
    WAVEFORMATEX wf;
    BYTE *waveData = NULL;
    DWORD waveSize = 0;

    while (pos + 8 <= size) {
        DWORD chunkSize = *(DWORD *)(data + pos + 4);
        if (memcmp(data + pos, "fmt ", 4) == 0) {
            memcpy(&wf, data + pos + 8, sizeof(WAVEFORMATEX));
        } else if (memcmp(data + pos, "data", 4) == 0) {
            waveData = data + pos + 8;
            waveSize = chunkSize;
        }
        pos += 8 + ((chunkSize + 1) & ~1); // ���[�h���E���킹
    }

    if (!waveData || waveSize == 0) {
        free(data);
        rb_raise(eDXRubyError, "No wave data found");
    }

    // �o�b�t�@�쐬
    DSBUFFERDESC desc = {0};
    desc.dwSize = sizeof(DSBUFFERDESC);
    desc.dwFlags = DSBCAPS_CTRLVOLUME;
    desc.dwBufferBytes = waveSize;
    desc.lpwfxFormat = &wf;

    IDirectSoundBuffer *pTmp = NULL;
    hr = g_pDSound->lpVtbl->CreateSoundBuffer(g_pDSound, &desc, &pTmp, NULL);
    if (FAILED(hr)) {
        free(data);
        rb_raise(eDXRubyError, "Failed to create sound buffer");
    }

    // ���b�N���ăR�s�[
    VOID *p1, *p2;
    DWORD b1, b2;
    hr = pTmp->lpVtbl->Lock(pTmp, 0, waveSize, &p1, &b1, &p2, &b2, 0);
    if (FAILED(hr)) {
        pTmp->lpVtbl->Release(pTmp);
        free(data);
        rb_raise(eDXRubyError, "Buffer lock failed");
    }
    memcpy(p1, waveData, b1);
    if (p2 && b2 > 0) memcpy(p2, waveData + b1, b2);
    pTmp->lpVtbl->Unlock(pTmp, p1, b1, p2, b2);

    sound->pDSBuffer  = pTmp;
    sound->vbuffer    = Qnil;
    sound->loopcount  = 0;        /* �J��Ԃ��Ȃ�(�P��̂ݍĐ�) */
    sound->midwavflag = 1;

    free(data);
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
   ���������A�ΐ��X�P�[���̂��ߎ��ۂɂ� -5000�ł͂قږ����ɂȂ�
   ��DXRuby�����ɂ́A0�`255�� -5000�`0�Ƀ}�b�s���O�����A
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

