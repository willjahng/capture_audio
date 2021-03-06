#include "pch.h"
#include <iostream>

// REFERENCE_TIME time units per second and per millisecond
#define REFTIMES_PER_SEC  10000000
#define REFTIMES_PER_MILLISEC  10000

#define EXIT_ON_ERROR(hres) if (FAILED(hres)) { goto Exit; }
#define SAFE_RELEASE(punk)  if ((punk) != NULL) { (punk)->Release(); (punk) = NULL; }

const CLSID CLSID_MMDeviceEnumerator = __uuidof(MMDeviceEnumerator);
const IID IID_IMMDeviceEnumerator    = __uuidof(IMMDeviceEnumerator);
const IID IID_IAudioClient           = __uuidof(IAudioClient);
const IID IID_IAudioCaptureClient    = __uuidof(IAudioCaptureClient);
const IID IID_IAudioRenderClient     = __uuidof(IAudioRenderClient);

HRESULT RecordAudioStream(void)
{
    HRESULT        hr;

    REFERENCE_TIME hnsRequestedDuration = REFTIMES_PER_SEC / 2 / 2; // AUDCLNT_E_BUFFER_SIZE_ERROR, The requested duration value for pull mode must not be greater than 500 milliseconds
    REFERENCE_TIME hnsActualDuration;
    UINT32         bufferFrameCount;
    UINT32         numFramesAvailable;

    IMMDeviceEnumerator *pEnumerator = NULL;
    IMMDevice           *pDevice = NULL;
    IAudioClient        *pAudioClient = NULL;
    IAudioCaptureClient *pCaptureClient = NULL;
    WAVEFORMATEX         wfx = { 0 };
    WAVEFORMATEX        *pwfx = NULL, *pwfxc = NULL;

    FILE                *fw = NULL;

    UINT32        packetLength = 0, cntSample = 0;
    BOOL          bDone = FALSE;
    BYTE         *pData;
    DWORD         flags;

    int cntRun = 15;

    hr = CoInitializeEx(NULL, COINIT_APARTMENTTHREADED | COINIT_DISABLE_OLE1DDE);
    EXIT_ON_ERROR(hr)

    hr = CoCreateInstance(CLSID_MMDeviceEnumerator, NULL, CLSCTX_ALL, IID_IMMDeviceEnumerator, (void**)&pEnumerator);
    EXIT_ON_ERROR(hr)

    hr = pEnumerator->GetDefaultAudioEndpoint(eCapture, eConsole, &pDevice);
    EXIT_ON_ERROR(hr)

    hr = pDevice->Activate(IID_IAudioClient, CLSCTX_ALL, NULL, (void**)&pAudioClient);
    EXIT_ON_ERROR(hr)

    if (1)
    {
        wfx.wFormatTag      = WAVE_FORMAT_PCM;
        wfx.nChannels       = 2;
        wfx.wBitsPerSample  = 16;
        wfx.nSamplesPerSec  = 192000; // 192000, 96000, 48000, 16000, 8000;
        wfx.nAvgBytesPerSec = wfx.wBitsPerSample / 8 * wfx.nSamplesPerSec * wfx.nChannels;
        wfx.nBlockAlign     = wfx.wBitsPerSample / 8 *                      wfx.nChannels;
        wfx.cbSize          = 0;

        hr = pAudioClient->IsFormatSupported(AUDCLNT_SHAREMODE_EXCLUSIVE, &wfx, &pwfxc);
        EXIT_ON_ERROR(hr)

        if (pwfxc)
        {
            pwfx = pwfxc;
        }
        else
        {
            pwfx = &wfx;
        }
    }
    else
    {
        hr = pAudioClient->GetMixFormat(&pwfxc);
        EXIT_ON_ERROR(hr)

        pwfx = pwfxc;
    }

    hr = pAudioClient->Initialize(
        AUDCLNT_SHAREMODE_EXCLUSIVE,
        0,
        hnsRequestedDuration,
        0,
        pwfx,
        NULL);
    EXIT_ON_ERROR(hr)

    hr = pAudioClient->GetBufferSize(&bufferFrameCount);
    EXIT_ON_ERROR(hr)

    hr = pAudioClient->GetService(IID_IAudioCaptureClient, (void**)&pCaptureClient);
    EXIT_ON_ERROR(hr)

    // Calculate the actual duration of the allocated buffer.
    hnsActualDuration = (double) REFTIMES_PER_SEC * bufferFrameCount / pwfx->nSamplesPerSec;

    hr = pAudioClient->Start();
    EXIT_ON_ERROR(hr)

    if (fw = fopen("capture.wav", "wb"))
    {
        DWORD header[] = {
            FCC('RIFF'), // RIFF header
            0,           // Total size of WAV (will be filled in later)
            FCC('WAVE'), // WAVE FourCC
            FCC('fmt '), // Start of 'fmt ' chunk
            sizeof(PCMWAVEFORMAT) 
        };

        PCMWAVEFORMAT wf = {
            {
                wfx.wFormatTag,
                wfx.nChannels,
                wfx.nSamplesPerSec,
                wfx.nAvgBytesPerSec,
                wfx.nBlockAlign
            },
            wfx.wBitsPerSample
        };

        DWORD data[] = { FCC('data'), wfx.nAvgBytesPerSec * 60 * 4 }; // Start of 'data' chunk

        size_t lenW, len;

        if ((lenW = fwrite((void *) header, 1, len = sizeof(header), fw)) != len)
        {
            printf("Dropped %u != %u (expected)\n", lenW, len);
        }
        else if ((lenW = fwrite((void *) &wf, 1, len = sizeof(wf), fw)) != len)
        {
            printf("Dropped %u != %u (expected)\n", lenW, len);
        }
        else if ((lenW = fwrite((void *) data, 1, len = sizeof(data), fw)) != len)
        {
            printf("Dropped %u != %u (expected)\n", lenW, len);
        }
    }
    else
    {
        printf("Failed in fopen()\n");
    }

    // Each loop fills about half of the buffer.
    while (!_kbhit())
    {
        // Sleep for half the buffer duration.
        Sleep(hnsActualDuration / REFTIMES_PER_MILLISEC / 2);

        hr = pCaptureClient->GetNextPacketSize(&packetLength);
        EXIT_ON_ERROR(hr)

        // printf("%u\n", packetLength);

        while (packetLength != 0)
        {
            hr = pCaptureClient->GetBuffer(&pData, &numFramesAvailable, &flags, NULL, NULL);
            EXIT_ON_ERROR(hr)

                if (flags & AUDCLNT_BUFFERFLAGS_SILENT)
                {
                    pData = NULL;
                }

#if 1
            {
                BYTE *p = pData;
                size_t lenW, len = numFramesAvailable * pwfx->nBlockAlign;

                if (fw && (lenW = fwrite((void*) p, 1, len, fw)) != len)
                {
                    printf("Dropped %u != %u (expected)\n", lenW, len);
                }

                cntSample += numFramesAvailable;

                {
                    static int t = 1;

                    if ((float) cntSample / pwfx->nSamplesPerSec > (float) t)
                    {
                        printf("%d sec (%u)\n", t++, cntSample);
                    }
                }
            }
#endif

            hr = pCaptureClient->ReleaseBuffer(numFramesAvailable);
            EXIT_ON_ERROR(hr)

            hr = pCaptureClient->GetNextPacketSize(&packetLength);
            EXIT_ON_ERROR(hr)
        }
    }

    printf("Stopped capture, %.1f sec (%d/%d/%d, %d)\n", 
        (float) cntSample / pwfx->nSamplesPerSec,
        pwfx->nSamplesPerSec,
        pwfx->wBitsPerSample,
        pwfx->nChannels,
        pwfx->nBlockAlign);

    hr = pAudioClient->Stop();
    EXIT_ON_ERROR(hr)

Exit:
    if (fw) fclose(fw);

    CoTaskMemFree(pwfxc);
    SAFE_RELEASE(pEnumerator)
    SAFE_RELEASE(pDevice)
    SAFE_RELEASE(pAudioClient)
    SAFE_RELEASE(pCaptureClient)

    CoUninitialize();

    return hr;
}

//
//
//

HRESULT PlayAudioStream(void)
{
    HRESULT        hr;

    REFERENCE_TIME hnsRequestedDuration = REFTIMES_PER_SEC / 2 / 2; // AUDCLNT_E_BUFFER_SIZE_ERROR, The requested duration value for pull mode must not be greater than 500 milliseconds
    REFERENCE_TIME hnsActualDuration;
    UINT32         bufferFrameCount;
    UINT32         numFramesAvailable;
    UINT32         numFramesPadding;

    IMMDeviceEnumerator *pEnumerator = NULL;
    IMMDevice           *pDevice = NULL;
    IAudioClient        *pAudioClient = NULL;
    IAudioRenderClient  *pRenderClient = NULL;
    WAVEFORMATEX         wfx = { 0 };
    WAVEFORMATEX        *pwfx = NULL, *pwfxc = NULL;

    FILE                *fw = NULL;

    UINT32        packetLength = 0, cntSample = 0;
    BOOL          bDone = FALSE;
    BYTE         *pData;
    DWORD         flags = 0;

    int cntRun = 15;

    hr = CoInitializeEx(NULL, COINIT_APARTMENTTHREADED | COINIT_DISABLE_OLE1DDE);
    EXIT_ON_ERROR(hr)

    hr = CoCreateInstance(CLSID_MMDeviceEnumerator, NULL, CLSCTX_ALL, IID_IMMDeviceEnumerator, (void**)&pEnumerator);
    EXIT_ON_ERROR(hr)

    hr = pEnumerator->GetDefaultAudioEndpoint(eRender, eConsole, &pDevice);
    EXIT_ON_ERROR(hr)

    hr = pDevice->Activate(IID_IAudioClient, CLSCTX_ALL, NULL, (void**)&pAudioClient);
    EXIT_ON_ERROR(hr)

    if (fw = fopen(".\\capture.wav", "rb"))
    {
        DWORD header[5] = { 0, };

        PCMWAVEFORMAT wf = { 0, };

        DWORD data[2] = { 0, }; // Start of 'data' chunk

        size_t lenW, len;

        if ((lenW = fread((void *) header, 1, len = sizeof(header), fw)) != len)
        {
            printf("Missed %u != %u (expected)\n", lenW, len);
        }
        else if ((lenW = fread((void *) &wf, 1, len = sizeof(wf), fw)) != len)
        {
            printf("Missed %u != %u (expected)\n", lenW, len);
        }
        else if ((lenW = fread((void *) data, 1, len = sizeof(data), fw)) != len)
        {
            printf("Missed %u != %u (expected)\n", lenW, len);
        }
        else
        {
            wfx.wFormatTag      = wf.wf.wFormatTag;
            wfx.nChannels       = wf.wf.nChannels;
            wfx.nSamplesPerSec  = wf.wf.nSamplesPerSec;
            wfx.nAvgBytesPerSec = wf.wf.nAvgBytesPerSec;
            wfx.nBlockAlign     = wf.wf.nBlockAlign;
            wfx.wBitsPerSample = wf.wBitsPerSample;
        }
    }
    else
    {
        printf("Failed in fopen()\n");
    }

    hr = pAudioClient->IsFormatSupported(AUDCLNT_SHAREMODE_EXCLUSIVE, &wfx, &pwfxc);
    EXIT_ON_ERROR(hr)

    if (pwfxc)
    {
        pwfx = pwfxc;
    }
    else
    {
        pwfx = &wfx;
    }

    hr = pAudioClient->Initialize(
        AUDCLNT_SHAREMODE_EXCLUSIVE,
        0,
        hnsRequestedDuration,
        0,
        pwfx,
        NULL);
    EXIT_ON_ERROR(hr)

    hr = pAudioClient->GetBufferSize(&bufferFrameCount);
    EXIT_ON_ERROR(hr)

    hr = pAudioClient->GetService(IID_IAudioRenderClient, (void**)&pRenderClient);
    EXIT_ON_ERROR(hr)

    {
        // Grab the entire buffer for the initial fill operation.
    }

    // Calculate the actual duration of the allocated buffer.
    hnsActualDuration = (double) REFTIMES_PER_SEC * bufferFrameCount / pwfx->nSamplesPerSec;

    hr = pAudioClient->Start();
    EXIT_ON_ERROR(hr)

    // Each loop fills about half of the buffer.
    while (!_kbhit())
    {
        // Sleep for half the buffer duration.
        Sleep(hnsActualDuration / REFTIMES_PER_MILLISEC / 2);

        hr = pAudioClient->GetCurrentPadding(&numFramesPadding);
        EXIT_ON_ERROR(hr)

        numFramesAvailable = bufferFrameCount - numFramesPadding;

        // printf("%u\n", packetLength);

        hr = pRenderClient->GetBuffer(numFramesAvailable, &pData);
        EXIT_ON_ERROR(hr)

        if (flags & AUDCLNT_BUFFERFLAGS_SILENT)
        {
        }

#if 1
        {
            BYTE *p = pData;
            size_t lenW, len = numFramesAvailable * pwfx->nBlockAlign;

            if (fw && (lenW = fread((void*) p, 1, len, fw)) != len)
            {
                printf("Missed %u != %u (expected)\n", lenW, len);
            }

            cntSample += numFramesAvailable;

            {
                static int t = 1;

                if ((float) cntSample / pwfx->nSamplesPerSec > (float) t)
                {
                    printf("%d sec (%u)\n", t++, cntSample);
                }
            }
        }
#endif

        hr = pRenderClient->ReleaseBuffer(numFramesAvailable, flags);
        EXIT_ON_ERROR(hr)
    }

    printf("Stopped playback, %.1f sec (%d/%d/%d, %d)\n", 
        (float) cntSample / pwfx->nSamplesPerSec,
        pwfx->nSamplesPerSec,
        pwfx->wBitsPerSample,
        pwfx->nChannels,
        pwfx->nBlockAlign);

    Sleep(hnsActualDuration / REFTIMES_PER_MILLISEC / 2); // for exhausting buffer

    hr = pAudioClient->Stop();
    EXIT_ON_ERROR(hr)

Exit:
    if (fw) fclose(fw);

    CoTaskMemFree(pwfxc);
    SAFE_RELEASE(pEnumerator)
    SAFE_RELEASE(pDevice)
    SAFE_RELEASE(pAudioClient)
    SAFE_RELEASE(pRenderClient)

    CoUninitialize();

    return hr;
}


int main()
{
    // RecordAudioStream();
    PlayAudioStream();
}