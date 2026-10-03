#pragma once

namespace psx
{
	// Owns the D3D11 device (shared with the Media Foundation decoder) and presents decoded NV12
	// frames into a XAML SwapChainPanel. Colour conversion and scaling are done by the hardware
	// video processor, so no shaders are needed.
	//
	// Frames are handed over through a single-slot mailbox and presented on a dedicated render
	// thread, so the network/decoder thread never blocks on vsync. If the renderer falls behind,
	// older frames are dropped in favour of the newest one.
	class VideoRenderer
	{
	public:
		VideoRenderer() = default;
		~VideoRenderer();

		bool InitDevice();
		ID3D11Device *Device() const { return device_.get(); }
		IMFDXGIDeviceManager *DeviceManager() const { return device_manager_.get(); }

		// UI thread only.
		void AttachPanel(winrt::Windows::UI::Xaml::Controls::SwapChainPanel const &panel);
		void Resize(float width, float height, float scale_x, float scale_y);

		void StartThread();
		void StopThread();

		// Called from the decoder thread. The sample keeps the decoder surface alive until shown.
		void SubmitGpuFrame(IMFSample *sample, ID3D11Texture2D *texture, UINT slice, UINT width, UINT height, RECT const &src);
		// Tightly packed NV12 (stride == width).
		void SubmitCpuFrame(std::vector<uint8_t> &&nv12, UINT width, UINT height, RECT const &src);

	private:
		struct Frame
		{
			winrt::com_ptr<IMFSample> sample;
			winrt::com_ptr<ID3D11Texture2D> texture;
			UINT slice = 0;
			std::vector<uint8_t> cpu_data;
			UINT width = 0;
			UINT height = 0;
			RECT src{};
		};

		void RenderLoop();
		void Draw(Frame &frame);
		bool EnsureSwapChain();
		bool EnsureVideoProcessor(UINT in_w, UINT in_h, UINT out_w, UINT out_h);
		ID3D11VideoProcessorInputView *InputView(ID3D11Texture2D *texture, UINT slice);
		void Clear();

		winrt::com_ptr<ID3D11Device> device_;
		winrt::com_ptr<ID3D11DeviceContext> context_;
		winrt::com_ptr<ID3D11VideoDevice> video_device_;
		winrt::com_ptr<ID3D11VideoContext> video_context_;
		winrt::com_ptr<IMFDXGIDeviceManager> device_manager_;
		UINT reset_token_ = 0;

		// Guarded by swap_mutex_.
		std::mutex swap_mutex_;
		winrt::com_ptr<IDXGISwapChain1> swap_chain_;
		winrt::com_ptr<ID3D11VideoProcessorOutputView> output_view_;
		UINT out_w_ = 0;
		UINT out_h_ = 0;
		float scale_x_ = 1.0f;
		float scale_y_ = 1.0f;
		winrt::Windows::UI::Xaml::Controls::SwapChainPanel panel_{ nullptr };

		winrt::com_ptr<ID3D11VideoProcessorEnumerator> vp_enum_;
		winrt::com_ptr<ID3D11VideoProcessor> vp_;
		UINT vp_in_w_ = 0, vp_in_h_ = 0, vp_out_w_ = 0, vp_out_h_ = 0;
		std::map<std::pair<ID3D11Texture2D *, UINT>, winrt::com_ptr<ID3D11VideoProcessorInputView>> input_views_;
		winrt::com_ptr<ID3D11Texture2D> upload_texture_;
		UINT upload_w_ = 0, upload_h_ = 0;

		// Mailbox
		std::mutex frame_mutex_;
		std::condition_variable frame_cv_;
		std::optional<Frame> pending_;
		bool stop_ = false;
		std::thread thread_;
	};
}
