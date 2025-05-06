#define WINVER 0x0500                                  /* バージョン定義 Windows2000以上 */
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

static VALUE cSoundEffect;  /* 生成効果音クラス     */
static VALUE cSound;        /* サウンドクラス       */

static LPDIRECTSOUND8 g_pDSound = NULL;   /* DirectSoundインターフェイス */
static int            g_iRefDS  = 0;      /* DirectSoundの参照カウント   */

/* Soundクラスの構造体 */
struct DXRubySound {
    LPDIRECTSOUNDBUFFER pDSBuffer;  /* DirectSoundバッファ        */
    int start;                      /* 開始位置(未使用)           */
    int loopstart;                  /* ループ開始位置(未使用)     */
    int loopend;                    /* ループ終了位置(未使用)     */
    int loopcount;                  /* ループ回数                 */
    int midwavflag;                 /* wav:1, mid: 0 (未使用)     */
    VALUE vbuffer;                  /* Rubyのバッファ             */
};

/* SoundEffectオブジェクト */
struct DXRubySoundEffect {
    LPDIRECTSOUNDBUFFER pDSBuffer;    /* バッファ         */
};


/*********************************************************************
 * Soundクラス
 *
 * 【改修概要】2025/05
 *    本家DXRubyで使用していた DirectMusicは
 *    Windows10/11 64bit環境では実質的に使用できないため、
 *    Soundクラスでも SoundEffectクラスと同様に
 *    DirectSoundを使用するよう改修した。
 *
 * 【DirectMusicを使用した場合のエラー例】
 *    'DXRuby::Sound#initialize': DirectMusic initialize error - CoCreateInstance (DXRuby::DXRubyError)
 *    HRESULT = 0x80040154 (COMコンポーネントがレジストリに登録されていない)
 *********************************************************************/


/*--------------------------------------------------------------------
   メモリの解放をおこない、リソースを解放
 ---------------------------------------------------------------------*/
static void Sound_free( struct DXRubySound *sound )
{
    /* サウンドバッファを開放 */
    RELEASE( sound->pDSBuffer );

    g_iRefDS--;   /* SoundEffectクラスと共通の参照カウンタを使用 */

    if( g_iRefDS <= 0 )
    {
        RELEASE( g_pDSound );
    }
}

/*--------------------------------------------------------------------
   ガーベジコレクタが対象のオブジェクトを追跡できるようにする
 ---------------------------------------------------------------------*/
static void Sound_mark( struct DXRubySound *sound )
{
    rb_gc_mark( sound->vbuffer );
}

/*--------------------------------------------------------------------
   ガーベジコレクタが対象のオブジェクトを解放する際に呼ばれる関数
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
   メモリ確保 (initialize前に呼ばれる)
 ---------------------------------------------------------------------*/
static VALUE Sound_allocate( VALUE klass )
{
    VALUE obj;
    struct DXRubySound *sound;

    /* DXRubySoundのメモリ確保 */
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
   オブジェクトの破棄
 ---------------------------------------------------------------------*/
static VALUE Sound_dispose( VALUE self )
{
    struct DXRubySound *sound = DXRUBY_GET_STRUCT( Sound, self );
    DXRUBY_CHECK_DISPOSE( sound, pDSBuffer );
    Sound_free( sound );
    return self;
}

/*--------------------------------------------------------------------
   オブジェクト破棄状況のチェック
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
   オブジェクト生成
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

    // Shift_JIS 変換
    if (rb_enc_get_index(vfilename) != 0) {
        vsjisstr = rb_str_export_to_enc(vfilename, g_enc_sys);
    } else {
        vsjisstr = vfilename;
    }

    filename = RSTRING_PTR(vsjisstr);

    // DirectSound 初期化
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

    // 構造体取得とクリーンアップ
    sound = DXRUBY_GET_STRUCT(Sound, obj);
    if (sound->pDSBuffer) {
        Sound_free(sound);
    }

    // ファイル読み込み
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

    // WAV ヘッダ解析
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
        pos += 8 + ((chunkSize + 1) & ~1); // ワード境界合わせ
    }

    if (!waveData || waveSize == 0) {
        free(data);
        rb_raise(eDXRubyError, "No wave data found");
    }

    // バッファ作成
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

    // ロックしてコピー
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
    sound->loopcount  = 0;        /* 繰り返しなし(１回のみ再生) */
    sound->midwavflag = 1;

    free(data);
    return obj;
}

/*--------------------------------------------------------------------
   再生
 ---------------------------------------------------------------------*/
static VALUE Sound_play(VALUE obj) {
    struct DXRubySound *sound;
    HRESULT hr;

    sound = DXRUBY_GET_STRUCT(Sound, obj);

    if (!sound->pDSBuffer) return Qnil;

    // 再生位置を先頭に戻す
    sound->pDSBuffer->lpVtbl->SetCurrentPosition(sound->pDSBuffer, 0);

    // loopcount が DSBPLAY_LOOPING か 0 でなければ、エラー
    if (sound->loopcount != DSBPLAY_LOOPING && sound->loopcount != 0) {
      rb_raise(rb_eRuntimeError, "Invalid loop count value. Must be DSBPLAY_LOOPING or 0.");
    }

    // 再生
    hr = sound->pDSBuffer->lpVtbl->Play(sound->pDSBuffer, 0, 0, sound->loopcount);

    if (FAILED(hr)) {
      rb_raise(rb_eRuntimeError, "Failed to play sound");
    }

    return Qnil;
}

/*--------------------------------------------------------------------
   停止
 ---------------------------------------------------------------------*/
static VALUE Sound_stop(VALUE obj) {
    struct DXRubySound *sound;
    sound = DXRUBY_GET_STRUCT(Sound, obj);

    if (!sound->pDSBuffer) return Qnil;

    sound->pDSBuffer->lpVtbl->Stop(sound->pDSBuffer);
    return Qnil;
}

/*--------------------------------------------------------------------
   音量設定

   ※DirectSoundでは -10000(DSBVOLUME_MIN)～0(最大)
   ※ただし、対数スケールのため実際には -5000ではほぼ無音になる
   ※DXRuby向けには、0～255を -5000～0にマッピングさせ、
     0のときのみ完全ミュートの -10000とする
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
        dsVolume = -10000;  // 完全ミュート
    } else {
        dsVolume = (LONG)((volume * 5000.0f / 255.0f) - 5000.0f);  // -5000 ～ 0
    }

    sound->pDSBuffer->lpVtbl->SetVolume(sound->pDSBuffer, dsVolume);

    return obj;
}

/*--------------------------------------------------------------------
   音量取得
 ---------------------------------------------------------------------*/
static VALUE Sound_getVolume(VALUE obj) {
    struct DXRubySound *sound = DXRUBY_GET_STRUCT(Sound, obj);
    LONG dsVolume;

    if (!sound->pDSBuffer) return INT2FIX(255); // バッファが未初期化なら最大音量

    if (FAILED(IDirectSoundBuffer_GetVolume(sound->pDSBuffer, &dsVolume))) {
        rb_raise(eDXRubyError, "Failed to get volume.");
    }

    // DirectSoundの -5000～0 を Rubyの 0～255 に変換
    int rubyVolume = (int)(((dsVolume + -5000.0f) * 255.0f / -5000.0f) + 0.5f);
    if (rubyVolume < 0) rubyVolume = 0;
    if (rubyVolume > 255) rubyVolume = 255;

    if (dsVolume <= -10000 + 100) {
        rubyVolume = 0;  // 完全ミュート扱い
    } else {
        rubyVolume = (int)(((dsVolume + 5000.0f) * 255.0f / 5000.0f) + 0.5f);  // -5000～0 → 0～255
    }
    // fprintf(stderr, "%d %ld -> %d\n", DSBVOLUME_MIN, dsVolume, rubyVolume);

    return INT2FIX(rubyVolume);
}

/*--------------------------------------------------------------------
   ループ回数の設定

   ※DirectMusicでは任意の回数指定ができたが、
     DirectSoundでは１回のみの再生または無限ループのいずれか

   ※本家DXRubyの仕様を踏襲したもの
     -1 は無限ループ、それ以外の数値はすべて 0 (実行回数１回)
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
   ループ採否の設定

   ※DirectMusicでは任意の回数指定ができたが、
     DirectSoundでは１回のみの再生または無限ループのいずれか

   ※直感的に boolean(true/false)で指定できるようにしたもの
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
 * SoundEffectクラス
 *
 * DirectSoundを使用して音を鳴らす。
 * とりあえず音を出そうと頑張っている。
 *********************************************************************/

/*--------------------------------------------------------------------
   参照されなくなったときにGCから呼ばれる関数
 ---------------------------------------------------------------------*/
static void SoundEffect_free( struct DXRubySoundEffect *soundeffect )
{
    /* サウンドバッファを開放 */
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
   SoundEffectクラスのdispose。
 ---------------------------------------------------------------------*/
static VALUE SoundEffect_dispose( VALUE self )
{
    struct DXRubySoundEffect *soundeffect = DXRUBY_GET_STRUCT( SoundEffect, self );
    DXRUBY_CHECK_DISPOSE( soundeffect, pDSBuffer );
    SoundEffect_free( soundeffect );
    return self;
}

/*--------------------------------------------------------------------
   SoundEffectクラスのdisposed?。
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
   SoundEffectクラスのallocate。メモリを確保する為にinitialize前に呼ばれる。
 ---------------------------------------------------------------------*/
static VALUE SoundEffect_allocate( VALUE klass )
{
    VALUE obj;
    struct DXRubySoundEffect *soundeffect;

    /* DXRubySoundEffectのメモリ取得＆SoundEffectオブジェクト生成 */
    soundeffect = malloc(sizeof(struct DXRubySoundEffect));
    if( soundeffect == NULL ) rb_raise( eDXRubyError, "Out of memory - SoundEffect_allocate" );
#ifdef DXRUBY_USE_TYPEDDATA
    obj = TypedData_Wrap_Struct( klass, &SoundEffect_data_type, soundeffect );
#else
    obj = Data_Wrap_Struct(klass, 0, SoundEffect_release, soundeffect);
#endif

    /* とりあえずサウンドオブジェクトはNULLにしておく */
    soundeffect->pDSBuffer = NULL;

    return obj;
}


static short calcwave(int type, double vol, double count, double p, double duty)
{
	switch( type )
	{
	case 1: /* サイン波 */
		return (short)((sin( (3.141592653589793115997963468544185161590576171875f * 2) * (double)count / (double)p )) * (double)vol * 128);
		break;
	case 2: /* ノコギリ波 */
		return (short)(((double)count / (double)p - 0.5) * (double)vol * 256);
		break;
	case 3: /* 三角波 */
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
		else											/* 最後 */
		{
			return (short)(-((double)p - (double)count) / ((double)p / 4) * (double)vol * 128);
		}
		break;
	case 0: /* 矩形波 */
	default: /* デフォルト */
		if( count < p * duty )	/* 前半 */
		{
			return (short)(vol * 128);
		}
		else									/* 後半 */
		{
		    return (short)(-vol * 128);
		}
		break;
	}
}


/*--------------------------------------------------------------------
   SoundEffectクラスのinitialize。波形を生成する。
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

    /* DirectSoundオブジェクトの作成 */
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

    /* サウンドオブジェクト作成 */
    soundeffect = DXRUBY_GET_STRUCT( SoundEffect, obj );
    if( soundeffect->pDSBuffer )
    {
        g_iRefDS++;
        SoundEffect_free( soundeffect );
        g_iRefDS--;
        g_iRefAll++;
    }

    /* サウンドバッファ作成 */
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

    /* ロック */
    hr = soundeffect->pDSBuffer->lpVtbl->Lock( soundeffect->pDSBuffer, 0, 0, (VOID**)&pointer, &size, (VOID**)&pointer2, &size2, DSBLOCK_ENTIREBUFFER );
    if( FAILED( hr ) || size2 != 0 )
    {
        rb_raise( eDXRubyError, "Failure to lock the SoundBuffer - Lock" );
    }

    if( TYPE( vsize ) == T_ARRAY )
    {
        /* 音バッファ生成 */
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
        /* 音バッファ初期化 */
        for( i = 0; i < desc.dwBufferBytes / (pcmwf.wBitsPerSample / 8); i++ )
        {
            pointer[i] = 0;
        }

        count = 0;

        /* 波形生成 */
        for( i = 0; i < desc.dwBufferBytes / (pcmwf.wBitsPerSample / 8); i++ )
        {
            /* 指定時間単位でブロックを呼び出す */
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
                /* 最大/最低周波数と最大ボリュームの制限 */
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

    /* アンロック */
    hr = soundeffect->pDSBuffer->lpVtbl->Unlock( soundeffect->pDSBuffer, pointer, size, pointer2, size2 );
    if( FAILED( hr ) )
    {
        rb_raise( eDXRubyError, "Failure to lock the SoundBuffer - Lock" );
    }

    return obj;

}


/*--------------------------------------------------------------------
   波形を合成する。
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

    /* サウンドオブジェクト取得 */
    soundeffect = DXRUBY_GET_STRUCT( SoundEffect, obj );
    DXRUBY_CHECK_DISPOSE( soundeffect, pDSBuffer );

	/* ロック */
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

	/* 波形生成 */
	for( i = 0; i < desc.dwBufferBytes / (pcmwf.wBitsPerSample / 8); i++ )
	{
		/* 指定時間単位でブロックを呼び出す */
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
			/* 最大/最低周波数と最大ボリュームの制限 */
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

	/* アンロック */
	hr = soundeffect->pDSBuffer->lpVtbl->Unlock( soundeffect->pDSBuffer, pointer, size, pointer2, size2 );
    if( FAILED( hr ) )
    {
        rb_raise( eDXRubyError, "Failure to lock the SoundBuffer - Lock" );
    }

    return obj;

}


/*--------------------------------------------------------------------
   音を鳴らす
 ---------------------------------------------------------------------*/
static VALUE SoundEffect_play( int argc, VALUE *argv, VALUE self )
{
    HRESULT hr;
    struct DXRubySoundEffect *se = DXRUBY_GET_STRUCT( SoundEffect, self );
    VALUE vflag;
    DXRUBY_CHECK_DISPOSE( se, pDSBuffer );

    rb_scan_args( argc, argv, "01", &vflag );

    /* とめる */
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

    /* 再生 */
    hr = se->pDSBuffer->lpVtbl->Play( se->pDSBuffer, 0, 0, RTEST(vflag) ? DSBPLAY_LOOPING : 0 );
    if( FAILED( hr ) )
    {
        rb_raise( eDXRubyError, "Sound play failed - SoundEffect_play" );
    }
    return self;
}


/*--------------------------------------------------------------------
   音を止める
 ---------------------------------------------------------------------*/
static VALUE SoundEffect_stop( VALUE obj )
{
    HRESULT hr;
    struct DXRubySoundEffect *soundeffect;

    soundeffect = DXRUBY_GET_STRUCT( SoundEffect, obj );
    DXRUBY_CHECK_DISPOSE( soundeffect, pDSBuffer );

    /* とめる */
    hr = soundeffect->pDSBuffer->lpVtbl->Stop( soundeffect->pDSBuffer );
    if( FAILED( hr ) )
    {
        rb_raise( eDXRubyError, "音の停止に失敗しました - Stop" );
    }

    return obj;
}


/*--------------------------------------------------------------------
   wavファイル出力
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

    /* ロック */
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

    /* アンロック */
    hr = se->pDSBuffer->lpVtbl->Unlock( se->pDSBuffer, pointer, size, pointer2, size2 );
    if( FAILED( hr ) )
    {
        rb_raise( eDXRubyError, "Failure to lock the SoundBuffer - Lock" );
    }

    return self;
}


/*--------------------------------------------------------------------
   配列化
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

    /* ロック */
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

    /* アンロック */
    hr = se->pDSBuffer->lpVtbl->Unlock( se->pDSBuffer, pointer, size, pointer2, size2 );
    if( FAILED( hr ) )
    {
        rb_raise( eDXRubyError, "Failure to lock the SoundBuffer - Lock" );
    }

    return ary;
}


void Init_dxruby_Sound( void )
{

    /* Soundクラス定義 */
    cSound = rb_define_class_under( mDXRuby, "Sound", rb_cObject );

    /* Soundクラスにメソッド登録*/
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

    /* Soundオブジェクトを生成した時にinitializeの前に呼ばれるメモリ割り当て関数登録 */
    rb_define_alloc_func( cSound, Sound_allocate );


    /* SoundEffectクラス定義 */
    cSoundEffect = rb_define_class_under( mDXRuby, "SoundEffect", rb_cObject );

    /* SoundEffectクラスにメソッド登録*/
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
    /* SoundEffectオブジェクトを生成した時にinitializeの前に呼ばれるメモリ割り当て関数登録 */
    rb_define_alloc_func( cSoundEffect, SoundEffect_allocate );

    rb_define_const( mDXRuby, "WAVE_SIN"     , INT2FIX(WAVE_SIN) );
    rb_define_const( mDXRuby, "WAVE_SAW"     , INT2FIX(WAVE_SAW) );
    rb_define_const( mDXRuby, "WAVE_TRI"     , INT2FIX(WAVE_TRI) );
    rb_define_const( mDXRuby, "WAVE_RECT"    , INT2FIX(WAVE_RECT) );

    rb_define_const( mDXRuby, "TYPE_MIDI"    , INT2FIX(0) );
    rb_define_const( mDXRuby, "TYPE_WAV"     , INT2FIX(1) );
}

