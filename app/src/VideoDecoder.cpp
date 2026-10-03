#include "pch.h"
#include "VideoDecoder.h"
#include "VideoRenderer.h"
#include "Logger.h"

using namespace winrt;

namespace psx
{
	VideoDecoder::~VideoDecoder()
	{
		Fini();
	}

	bool VideoDecoder::Init(ChiakiCodec codec, UINT width, UINT height, VideoRenderer *renderer)
	{
		Fini();
		renderer_ = renderer;

		const GUID subtype = chiaki_codec_is_h265(codec) ? MFVideoFormat_HEVC : MFVideoFormat_H264;
		MFT_REGISTER_TYPE_INFO in_info = { MFMediaType_Video, subtype };
		IMFActivate **activates = nullptr;
		UINT32 count = 0;
		HRESULT hr = MFTEnumEx(MFT_CATEGORY_VIDEO_DECODER,
			MFT_ENUM_FLAG_SYNCMFT | MFT_ENUM_FLAG_LOCALMFT | MFT_ENUM_FLAG_SORTANDFILTER,
			&in_info, nullptr, &activates, &count);
		if(FAILED(hr) || count == 0)
		{
			PSX_LOGE("No %s decoder available (0x%08x)%s", chiaki_codec_name(codec), hr,
				chiaki_codec_is_h265(codec) ? ". On PC install 'HEVC Video Extensions', or switch to H.264 in Settings." : "");
			return false;
		}
		hr = activates[0]->ActivateObject(IID_PPV_ARGS(mft_.put()));
		for(UINT32 i = 0; i < count; i++)
			activates[i]->Release();
		CoTaskMemFree(activates);
		if(FAILED(hr))
		{
			PSX_LOGE("Failed to activate decoder: 0x%08x", hr);
			return false;
		}

		com_ptr<IMFAttributes> attrs;
		if(SUCCEEDED(mft_->GetAttributes(attrs.put())))
		{
			attrs->SetUINT32(MF_LOW_LATENCY, TRUE);
			UINT32 d3d11_aware = 0;
			if(renderer_->DeviceManager() && SUCCEEDED(attrs->GetUINT32(MF_SA_D3D11_AWARE, &d3d11_aware)) && d3d11_aware)
			{
				hr = mft_->ProcessMessage(MFT_MESSAGE_SET_D3D_MANAGER, reinterpret_cast<ULONG_PTR>(renderer_->DeviceManager()));
				gpu_ = SUCCEEDED(hr);
				if(!gpu_)
					PSX_LOGI("Decoder rejected D3D11 device (0x%08x), using system memory", hr);
			}
		}
		if(auto codec_api = mft_.try_as<ICodecAPI>())
		{
			VARIANT v;
			VariantInit(&v);
			v.vt = VT_UI4;
			v.ulVal = 1;
			codec_api->SetValue(&CODECAPI_AVLowLatencyMode, &v);
		}

		com_ptr<IMFMediaType> in_type;
		check_hresult(MFCreateMediaType(in_type.put()));
		in_type->SetGUID(MF_MT_MAJOR_TYPE, MFMediaType_Video);
		in_type->SetGUID(MF_MT_SUBTYPE, subtype);
		MFSetAttributeSize(in_type.get(), MF_MT_FRAME_SIZE, width, height);
		in_type->SetUINT32(MF_MT_INTERLACE_MODE, MFVideoInterlace_Progressive);
		hr = mft_->SetInputType(0, in_type.get(), 0);
		if(FAILED(hr))
		{
			PSX_LOGE("Decoder SetInputType failed: 0x%08x", hr);
			mft_ = nullptr;
			return false;
		}

		if(gpu_)
		{
			com_ptr<IMFAttributes> out_attrs;
			if(SUCCEEDED(mft_->GetOutputStreamAttributes(0, out_attrs.put())))
				out_attrs->SetUINT32(MF_SA_MINIMUM_OUTPUT_SAMPLE_COUNT, 4); // the renderer holds up to 2
		}

		if(!SetOutputType())
		{
			mft_ = nullptr;
			return false;
		}

		mft_->ProcessMessage(MFT_MESSAGE_NOTIFY_BEGIN_STREAMING, 0);
		mft_->ProcessMessage(MFT_MESSAGE_NOTIFY_START_OF_STREAM, 0);

		PSX_LOGI("Video decoder ready: %s %ux%u, %s", chiaki_codec_name(codec), width, height, gpu_ ? "GPU surfaces" : "system memory");
		return true;
	}

	void VideoDecoder::Fini()
	{
		if(!mft_)
			return;
		mft_->ProcessMessage(MFT_MESSAGE_NOTIFY_END_OF_STREAM, 0);
		mft_->ProcessMessage(MFT_MESSAGE_COMMAND_FLUSH, 0);
		if(gpu_)
			mft_->ProcessMessage(MFT_MESSAGE_SET_D3D_MANAGER, 0);
		mft_ = nullptr;
		gpu_ = false;
		frames_ = 0;
	}

	bool VideoDecoder::SetOutputType()
	{
		for(DWORD i = 0;; i++)
		{
			com_ptr<IMFMediaType> type;
			HRESULT hr = mft_->GetOutputAvailableType(0, i, type.put());
			if(hr == MF_E_NO_MORE_TYPES)
				break;
			if(FAILED(hr))
				return false;
			GUID subtype = {};
			type->GetGUID(MF_MT_SUBTYPE, &subtype);
			if(subtype != MFVideoFormat_NV12)
				continue;
			hr = mft_->SetOutputType(0, type.get(), 0);
			if(FAILED(hr))
			{
				PSX_LOGE("Decoder SetOutputType failed: 0x%08x", hr);
				return false;
			}

			UINT32 w = 0, h = 0;
			MFGetAttributeSize(type.get(), MF_MT_FRAME_SIZE, &w, &h);
			frame_w_ = w;
			frame_h_ = h;
			UINT32 stride = 0;
			stride_ = SUCCEEDED(type->GetUINT32(MF_MT_DEFAULT_STRIDE, &stride)) ? static_cast<LONG>(stride) : static_cast<LONG>(w);

			MFVideoArea area = {};
			if(SUCCEEDED(type->GetBlob(MF_MT_MINIMUM_DISPLAY_APERTURE, reinterpret_cast<UINT8 *>(&area), sizeof(area), nullptr)))
				aperture_ = { area.OffsetX.value, area.OffsetY.value, area.OffsetX.value + area.Area.cx, area.OffsetY.value + area.Area.cy };
			else
				aperture_ = { 0, 0, static_cast<LONG>(w), static_cast<LONG>(h) };

			MFT_OUTPUT_STREAM_INFO info = {};
			mft_->GetOutputStreamInfo(0, &info);
			provides_samples_ = (info.dwFlags & (MFT_OUTPUT_STREAM_PROVIDES_SAMPLES | MFT_OUTPUT_STREAM_CAN_PROVIDE_SAMPLES)) != 0;
			output_buffer_size_ = info.cbSize;
			return true;
		}
		PSX_LOGE("Decoder offers no NV12 output");
		return false;
	}

	bool VideoDecoder::Decode(const uint8_t *data, size_t size)
	{
		if(!mft_)
			return false;

		com_ptr<IMFMediaBuffer> buffer;
		if(FAILED(MFCreateMemoryBuffer(static_cast<DWORD>(size), buffer.put())))
			return false;
		BYTE *dst = nullptr;
		buffer->Lock(&dst, nullptr, nullptr);
		memcpy(dst, data, size);
		buffer->Unlock();
		buffer->SetCurrentLength(static_cast<DWORD>(size));

		com_ptr<IMFSample> sample;
		if(FAILED(MFCreateSample(sample.put())))
			return false;
		sample->AddBuffer(buffer.get());
		sample->SetSampleTime(pts_);
		sample->SetSampleDuration(166667);
		pts_ += 166667;

		HRESULT hr = mft_->ProcessInput(0, sample.get(), 0);
		if(hr == MF_E_NOTACCEPTING)
		{
			if(!DrainOutput())
				return false;
			hr = mft_->ProcessInput(0, sample.get(), 0);
		}
		if(FAILED(hr))
		{
			PSX_LOGE("Decoder ProcessInput failed: 0x%08x", hr);
			return false;
		}
		return DrainOutput();
	}

	bool VideoDecoder::DrainOutput()
	{
		for(;;)
		{
			MFT_OUTPUT_DATA_BUFFER out = {};
			com_ptr<IMFSample> own_sample;
			if(!provides_samples_)
			{
				com_ptr<IMFMediaBuffer> buf;
				if(FAILED(MFCreateMemoryBuffer(output_buffer_size_, buf.put())) || FAILED(MFCreateSample(own_sample.put())))
					return false;
				own_sample->AddBuffer(buf.get());
				out.pSample = own_sample.get();
			}

			DWORD status = 0;
			HRESULT hr = mft_->ProcessOutput(0, 1, &out, &status);
			if(out.pEvents)
				out.pEvents->Release();

			if(hr == MF_E_TRANSFORM_NEED_MORE_INPUT)
				return true;
			if(hr == MF_E_TRANSFORM_STREAM_CHANGE)
			{
				if(provides_samples_ && out.pSample)
					out.pSample->Release();
				if(!SetOutputType())
					return false;
				PSX_LOGI("Decoder output changed: %ux%u", frame_w_, frame_h_);
				continue;
			}
			if(FAILED(hr))
			{
				if(provides_samples_ && out.pSample)
					out.pSample->Release();
				PSX_LOGE("Decoder ProcessOutput failed: 0x%08x", hr);
				return false;
			}

			if(out.pSample)
			{
				Deliver(out.pSample);
				if(provides_samples_)
					out.pSample->Release();
			}
		}
	}

	void VideoDecoder::Deliver(IMFSample *sample)
	{
		if(frames_++ == 0)
			PSX_LOGI("First video frame decoded");

		com_ptr<IMFMediaBuffer> buffer;
		if(FAILED(sample->GetBufferByIndex(0, buffer.put())))
			return;

		if(auto dxgi = buffer.try_as<IMFDXGIBuffer>())
		{
			com_ptr<ID3D11Texture2D> texture;
			UINT slice = 0;
			if(SUCCEEDED(dxgi->GetResource(IID_PPV_ARGS(texture.put()))) && SUCCEEDED(dxgi->GetSubresourceIndex(&slice)))
			{
				renderer_->SubmitGpuFrame(sample, texture.get(), slice, frame_w_, frame_h_, aperture_);
				return;
			}
		}

		// System memory NV12: repack tightly (stride == width) for the renderer.
		BYTE *src = nullptr;
		LONG pitch = 0;
		com_ptr<IMF2DBuffer> buf2d = buffer.try_as<IMF2DBuffer>();
		DWORD len = 0;
		if(buf2d)
		{
			if(FAILED(buf2d->Lock2D(&src, &pitch)))
				return;
		}
		else
		{
			if(FAILED(buffer->Lock(&src, nullptr, &len)))
				return;
			pitch = stride_ > 0 ? stride_ : static_cast<LONG>(frame_w_);
		}

		std::vector<uint8_t> nv12(static_cast<size_t>(frame_w_) * frame_h_ * 3 / 2);
		uint8_t *dst = nv12.data();
		for(UINT y = 0; y < frame_h_; y++)
			memcpy(dst + static_cast<size_t>(y) * frame_w_, src + static_cast<size_t>(y) * pitch, frame_w_);
		const BYTE *uv_src = src + static_cast<size_t>(pitch) * frame_h_;
		uint8_t *uv_dst = dst + static_cast<size_t>(frame_w_) * frame_h_;
		for(UINT y = 0; y < frame_h_ / 2; y++)
			memcpy(uv_dst + static_cast<size_t>(y) * frame_w_, uv_src + static_cast<size_t>(y) * pitch, frame_w_);

		if(buf2d)
			buf2d->Unlock2D();
		else
			buffer->Unlock();

		renderer_->SubmitCpuFrame(std::move(nv12), frame_w_, frame_h_, aperture_);
	}
}
