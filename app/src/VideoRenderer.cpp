#include "pch.h"
#include "VideoRenderer.h"
#include "Logger.h"

using namespace winrt;
using namespace Windows::UI::Xaml::Controls;

namespace psx
{
	VideoRenderer::~VideoRenderer()
	{
		StopThread();
	}

	bool VideoRenderer::InitDevice()
	{
		if(device_)
			return true;

		D3D_FEATURE_LEVEL levels[] = { D3D_FEATURE_LEVEL_11_1, D3D_FEATURE_LEVEL_11_0, D3D_FEATURE_LEVEL_10_1 };
		UINT flags = D3D11_CREATE_DEVICE_BGRA_SUPPORT | D3D11_CREATE_DEVICE_VIDEO_SUPPORT;
		HRESULT hr = D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, flags, levels, ARRAYSIZE(levels),
			D3D11_SDK_VERSION, device_.put(), nullptr, context_.put());
		if(FAILED(hr))
		{
			PSX_LOGE("D3D11CreateDevice failed: 0x%08x", hr);
			return false;
		}

		// The decoder uses the device from its own threads.
		if(auto mt = device_.try_as<ID3D10Multithread>())
			mt->SetMultithreadProtected(TRUE);

		video_device_ = device_.try_as<ID3D11VideoDevice>();
		video_context_ = context_.try_as<ID3D11VideoContext>();
		if(!video_device_ || !video_context_)
		{
			PSX_LOGE("GPU has no D3D11 video support");
			return false;
		}

		hr = MFCreateDXGIDeviceManager(&reset_token_, device_manager_.put());
		if(SUCCEEDED(hr))
			hr = device_manager_->ResetDevice(device_.get(), reset_token_);
		if(FAILED(hr))
		{
			PSX_LOGE("MFCreateDXGIDeviceManager failed: 0x%08x", hr);
			device_manager_ = nullptr;
		}
		return true;
	}

	void VideoRenderer::AttachPanel(SwapChainPanel const &panel)
	{
		std::lock_guard<std::mutex> lock(swap_mutex_);
		panel_ = panel;
		auto w = panel.ActualWidth() * panel.CompositionScaleX();
		auto h = panel.ActualHeight() * panel.CompositionScaleY();
		scale_x_ = panel.CompositionScaleX();
		scale_y_ = panel.CompositionScaleY();
		out_w_ = std::max<UINT>(1, static_cast<UINT>(w));
		out_h_ = std::max<UINT>(1, static_cast<UINT>(h));

		if(!swap_chain_)
		{
			com_ptr<IDXGIDevice> dxgi_device = device_.as<IDXGIDevice>();
			com_ptr<IDXGIAdapter> adapter;
			check_hresult(dxgi_device->GetAdapter(adapter.put()));
			com_ptr<IDXGIFactory2> factory;
			check_hresult(adapter->GetParent(IID_PPV_ARGS(factory.put())));

			DXGI_SWAP_CHAIN_DESC1 desc = {};
			desc.Width = out_w_;
			desc.Height = out_h_;
			desc.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
			desc.SampleDesc.Count = 1;
			desc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
			desc.BufferCount = 2;
			desc.Scaling = DXGI_SCALING_STRETCH;
			desc.SwapEffect = DXGI_SWAP_EFFECT_FLIP_SEQUENTIAL;
			desc.AlphaMode = DXGI_ALPHA_MODE_IGNORE;
			check_hresult(factory->CreateSwapChainForComposition(device_.get(), &desc, nullptr, swap_chain_.put()));
		}

		// Render at physical pixel resolution; tell composition to undo the DPI scale.
		if(auto sc2 = swap_chain_.try_as<IDXGISwapChain2>())
		{
			DXGI_MATRIX_3X2_F m = {};
			m._11 = 1.0f / scale_x_;
			m._22 = 1.0f / scale_y_;
			sc2->SetMatrixTransform(&m);
		}

		auto native = panel.as<ISwapChainPanelNative>();
		check_hresult(native->SetSwapChain(swap_chain_.get()));
		output_view_ = nullptr;
		PSX_LOGI("Video output %ux%u (scale %.2f)", out_w_, out_h_, scale_x_);
	}

	void VideoRenderer::Resize(float width, float height, float scale_x, float scale_y)
	{
		std::lock_guard<std::mutex> lock(swap_mutex_);
		UINT w = std::max<UINT>(1, static_cast<UINT>(width * scale_x));
		UINT h = std::max<UINT>(1, static_cast<UINT>(height * scale_y));
		if(!swap_chain_ || (w == out_w_ && h == out_h_ && scale_x == scale_x_ && scale_y == scale_y_))
			return;
		output_view_ = nullptr;
		HRESULT hr = swap_chain_->ResizeBuffers(0, w, h, DXGI_FORMAT_UNKNOWN, 0);
		if(FAILED(hr))
		{
			PSX_LOGE("ResizeBuffers failed: 0x%08x", hr);
			return;
		}
		out_w_ = w;
		out_h_ = h;
		scale_x_ = scale_x;
		scale_y_ = scale_y;
		if(auto sc2 = swap_chain_.try_as<IDXGISwapChain2>())
		{
			DXGI_MATRIX_3X2_F m = {};
			m._11 = 1.0f / scale_x_;
			m._22 = 1.0f / scale_y_;
			sc2->SetMatrixTransform(&m);
		}
	}

	void VideoRenderer::StartThread()
	{
		StopThread();
		stop_ = false;
		thread_ = std::thread([this] { RenderLoop(); });
	}

	void VideoRenderer::StopThread()
	{
		{
			std::lock_guard<std::mutex> lock(frame_mutex_);
			stop_ = true;
			pending_.reset();
		}
		frame_cv_.notify_all();
		if(thread_.joinable())
			thread_.join();

		std::lock_guard<std::mutex> lock(swap_mutex_);
		input_views_.clear();
		Clear();
	}

	void VideoRenderer::SubmitGpuFrame(IMFSample *sample, ID3D11Texture2D *texture, UINT slice, UINT width, UINT height, RECT const &src)
	{
		Frame f;
		f.sample.copy_from(sample);
		f.texture.copy_from(texture);
		f.slice = slice;
		f.width = width;
		f.height = height;
		f.src = src;
		{
			std::lock_guard<std::mutex> lock(frame_mutex_);
			pending_ = std::move(f);
		}
		frame_cv_.notify_one();
	}

	void VideoRenderer::SubmitCpuFrame(std::vector<uint8_t> &&nv12, UINT width, UINT height, RECT const &src)
	{
		Frame f;
		f.cpu_data = std::move(nv12);
		f.width = width;
		f.height = height;
		f.src = src;
		{
			std::lock_guard<std::mutex> lock(frame_mutex_);
			pending_ = std::move(f);
		}
		frame_cv_.notify_one();
	}

	void VideoRenderer::RenderLoop()
	{
		for(;;)
		{
			Frame frame;
			{
				std::unique_lock<std::mutex> lock(frame_mutex_);
				frame_cv_.wait(lock, [this] { return stop_ || pending_.has_value(); });
				if(stop_)
					return;
				frame = std::move(*pending_);
				pending_.reset();
			}
			Draw(frame);
		}
	}

	bool VideoRenderer::EnsureSwapChain()
	{
		if(!swap_chain_)
			return false;
		if(output_view_)
			return true;
		com_ptr<ID3D11Texture2D> back;
		if(FAILED(swap_chain_->GetBuffer(0, IID_PPV_ARGS(back.put()))))
			return false;
		D3D11_VIDEO_PROCESSOR_OUTPUT_VIEW_DESC desc = {};
		desc.ViewDimension = D3D11_VPOV_DIMENSION_TEXTURE2D;
		desc.Texture2D.MipSlice = 0;
		if(!vp_enum_)
			return false;
		HRESULT hr = video_device_->CreateVideoProcessorOutputView(back.get(), vp_enum_.get(), &desc, output_view_.put());
		if(FAILED(hr))
		{
			PSX_LOGE("CreateVideoProcessorOutputView failed: 0x%08x", hr);
			return false;
		}
		return true;
	}

	bool VideoRenderer::EnsureVideoProcessor(UINT in_w, UINT in_h, UINT out_w, UINT out_h)
	{
		if(vp_ && in_w == vp_in_w_ && in_h == vp_in_h_ && out_w == vp_out_w_ && out_h == vp_out_h_)
			return true;

		vp_ = nullptr;
		vp_enum_ = nullptr;
		output_view_ = nullptr;
		input_views_.clear();

		D3D11_VIDEO_PROCESSOR_CONTENT_DESC desc = {};
		desc.InputFrameFormat = D3D11_VIDEO_FRAME_FORMAT_PROGRESSIVE;
		desc.InputFrameRate = { 60, 1 };
		desc.InputWidth = in_w;
		desc.InputHeight = in_h;
		desc.OutputFrameRate = { 60, 1 };
		desc.OutputWidth = out_w;
		desc.OutputHeight = out_h;
		desc.Usage = D3D11_VIDEO_USAGE_OPTIMAL_SPEED;
		HRESULT hr = video_device_->CreateVideoProcessorEnumerator(&desc, vp_enum_.put());
		if(SUCCEEDED(hr))
			hr = video_device_->CreateVideoProcessor(vp_enum_.get(), 0, vp_.put());
		if(FAILED(hr))
		{
			PSX_LOGE("Failed to create video processor: 0x%08x", hr);
			vp_ = nullptr;
			vp_enum_ = nullptr;
			return false;
		}

		// Remote Play streams are BT.709 limited range.
		D3D11_VIDEO_PROCESSOR_COLOR_SPACE in_cs = {};
		in_cs.YCbCr_Matrix = 1;
		in_cs.Nominal_Range = D3D11_VIDEO_PROCESSOR_NOMINAL_RANGE_16_235;
		video_context_->VideoProcessorSetStreamColorSpace(vp_.get(), 0, &in_cs);
		D3D11_VIDEO_PROCESSOR_COLOR_SPACE out_cs = {};
		out_cs.RGB_Range = 0;
		video_context_->VideoProcessorSetOutputColorSpace(vp_.get(), &out_cs);
		video_context_->VideoProcessorSetStreamFrameFormat(vp_.get(), 0, D3D11_VIDEO_FRAME_FORMAT_PROGRESSIVE);
		video_context_->VideoProcessorSetStreamAutoProcessingMode(vp_.get(), 0, FALSE);
		D3D11_VIDEO_COLOR black = {};
		black.RGBA.A = 1.0f;
		video_context_->VideoProcessorSetOutputBackgroundColor(vp_.get(), FALSE, &black);

		vp_in_w_ = in_w;
		vp_in_h_ = in_h;
		vp_out_w_ = out_w;
		vp_out_h_ = out_h;
		return true;
	}

	ID3D11VideoProcessorInputView *VideoRenderer::InputView(ID3D11Texture2D *texture, UINT slice)
	{
		auto key = std::make_pair(texture, slice);
		auto it = input_views_.find(key);
		if(it != input_views_.end())
			return it->second.get();

		// Decoder surface pools are small; if this grows the pool was recreated.
		if(input_views_.size() > 64)
			input_views_.clear();

		D3D11_VIDEO_PROCESSOR_INPUT_VIEW_DESC desc = {};
		desc.FourCC = 0;
		desc.ViewDimension = D3D11_VPIV_DIMENSION_TEXTURE2D;
		desc.Texture2D.MipSlice = 0;
		desc.Texture2D.ArraySlice = slice;
		com_ptr<ID3D11VideoProcessorInputView> view;
		HRESULT hr = video_device_->CreateVideoProcessorInputView(texture, vp_enum_.get(), &desc, view.put());
		if(FAILED(hr))
		{
			PSX_LOGE("CreateVideoProcessorInputView failed: 0x%08x", hr);
			return nullptr;
		}
		auto raw = view.get();
		input_views_[key] = std::move(view);
		return raw;
	}

	void VideoRenderer::Draw(Frame &frame)
	{
		std::lock_guard<std::mutex> lock(swap_mutex_);
		if(!swap_chain_)
			return;

		ID3D11Texture2D *texture = frame.texture.get();
		UINT slice = frame.slice;

		if(!frame.cpu_data.empty())
		{
			if(!upload_texture_ || upload_w_ != frame.width || upload_h_ != frame.height)
			{
				input_views_.clear();
				upload_texture_ = nullptr;
				D3D11_TEXTURE2D_DESC td = {};
				td.Width = frame.width;
				td.Height = frame.height;
				td.MipLevels = 1;
				td.ArraySize = 1;
				td.Format = DXGI_FORMAT_NV12;
				td.SampleDesc.Count = 1;
				td.Usage = D3D11_USAGE_DEFAULT;
				td.BindFlags = D3D11_BIND_SHADER_RESOURCE;
				if(FAILED(device_->CreateTexture2D(&td, nullptr, upload_texture_.put())))
				{
					PSX_LOGE("Failed to create NV12 upload texture");
					return;
				}
				upload_w_ = frame.width;
				upload_h_ = frame.height;
			}
			context_->UpdateSubresource(upload_texture_.get(), 0, nullptr, frame.cpu_data.data(), frame.width, 0);
			texture = upload_texture_.get();
			slice = 0;
		}

		if(!texture)
			return;
		if(!EnsureVideoProcessor(frame.width, frame.height, out_w_, out_h_))
			return;
		if(!EnsureSwapChain())
			return;
		ID3D11VideoProcessorInputView *in_view = InputView(texture, slice);
		if(!in_view)
			return;

		// Letterbox to keep the aspect ratio.
		LONG src_w = frame.src.right - frame.src.left;
		LONG src_h = frame.src.bottom - frame.src.top;
		RECT dst = { 0, 0, static_cast<LONG>(out_w_), static_cast<LONG>(out_h_) };
		if(src_w > 0 && src_h > 0)
		{
			double src_aspect = static_cast<double>(src_w) / src_h;
			double out_aspect = static_cast<double>(out_w_) / out_h_;
			if(out_aspect > src_aspect)
			{
				LONG w = static_cast<LONG>(out_h_ * src_aspect);
				dst.left = (static_cast<LONG>(out_w_) - w) / 2;
				dst.right = dst.left + w;
			}
			else
			{
				LONG h = static_cast<LONG>(out_w_ / src_aspect);
				dst.top = (static_cast<LONG>(out_h_) - h) / 2;
				dst.bottom = dst.top + h;
			}
		}

		RECT target = { 0, 0, static_cast<LONG>(out_w_), static_cast<LONG>(out_h_) };
		video_context_->VideoProcessorSetOutputTargetRect(vp_.get(), TRUE, &target);
		video_context_->VideoProcessorSetStreamSourceRect(vp_.get(), 0, TRUE, &frame.src);
		video_context_->VideoProcessorSetStreamDestRect(vp_.get(), 0, TRUE, &dst);

		D3D11_VIDEO_PROCESSOR_STREAM stream = {};
		stream.Enable = TRUE;
		stream.pInputSurface = in_view;
		HRESULT hr = video_context_->VideoProcessorBlt(vp_.get(), output_view_.get(), 0, 1, &stream);
		if(FAILED(hr))
		{
			PSX_LOGE("VideoProcessorBlt failed: 0x%08x", hr);
			return;
		}

		hr = swap_chain_->Present(1, 0);
		if(hr == DXGI_ERROR_DEVICE_REMOVED || hr == DXGI_ERROR_DEVICE_RESET)
			PSX_LOGE("GPU device lost (0x%08x)", device_->GetDeviceRemovedReason());
	}

	void VideoRenderer::Clear()
	{
		if(!swap_chain_)
			return;
		com_ptr<ID3D11Texture2D> back;
		if(FAILED(swap_chain_->GetBuffer(0, IID_PPV_ARGS(back.put()))))
			return;
		com_ptr<ID3D11RenderTargetView> rtv;
		if(FAILED(device_->CreateRenderTargetView(back.get(), nullptr, rtv.put())))
			return;
		float black[4] = { 0, 0, 0, 1 };
		context_->ClearRenderTargetView(rtv.get(), black);
		swap_chain_->Present(0, 0);
	}
}
