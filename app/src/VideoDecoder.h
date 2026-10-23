#pragma once

#include "VideoRenderer.h"

namespace psx
{
	// Hardware H.264 / HEVC decoding through a Media Foundation transform. HDR streams are decoded to
	// 10-bit P010, the rest to NV12.
	// When the decoder accepts our D3D11 device it outputs GPU textures (zero copy); otherwise
	// frames come back in system memory and are uploaded by the renderer.
	class VideoDecoder
	{
	public:
		VideoDecoder() = default;
		~VideoDecoder();

		bool Init(ChiakiCodec codec, UINT width, UINT height, VideoRenderer *renderer);
		void Fini();

		// One Annex-B access unit from chiaki. Called on chiaki's video thread.
		bool Decode(const uint8_t *data, size_t size);

	private:
		bool SetOutputType();
		bool DrainOutput();
		void Deliver(IMFSample *sample);

		winrt::com_ptr<IMFTransform> mft_;
		VideoRenderer *renderer_ = nullptr;
		bool gpu_ = false;
		bool hdr_codec_ = false;
		VideoFormat format_;
		bool provides_samples_ = false;
		DWORD output_buffer_size_ = 0;
		UINT frame_w_ = 0;
		UINT frame_h_ = 0;
		LONG stride_ = 0;
		RECT aperture_{};
		LONGLONG pts_ = 0;
		uint64_t frames_ = 0;
	};
}
