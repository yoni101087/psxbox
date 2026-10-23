#include "pch.h"
#include "MicCapture.h"
#include "Logger.h"

#include <MemoryBuffer.h>

// Fully qualified: MemoryBuffer.h declares a global ::Windows::Foundation of its own.
using namespace winrt;
using namespace winrt::Windows::Foundation;
using namespace winrt::Windows::Media;
using namespace winrt::Windows::Media::Audio;
using namespace winrt::Windows::Media::Capture;
using namespace winrt::Windows::Media::MediaProperties;
using namespace winrt::Windows::Media::Render;

namespace psx
{
	MicCapture::~MicCapture()
	{
		Stop();
	}

	IAsyncOperation<bool> MicCapture::StartAsync(FrameCallback cb)
	{
		Stop();
		cb_ = std::move(cb);

		// AudioGraph works in 32-bit float; the samples are converted to 16-bit below.
		AudioEncodingProperties format = AudioEncodingProperties::CreatePcm(48000, 2, 32);
		format.Subtype(MediaEncodingSubtypes::Float());
		AudioGraphSettings settings(AudioRenderCategory::Communications);
		settings.EncodingProperties(format);
		settings.QuantumSizeSelectionMode(QuantumSizeSelectionMode::LowestLatency);

		auto graph_result = co_await AudioGraph::CreateAsync(settings);
		if(graph_result.Status() != AudioGraphCreationStatus::Success)
		{
			PSX_LOGE("Microphone: AudioGraph failed (%d)", static_cast<int>(graph_result.Status()));
			co_return false;
		}
		AudioGraph graph = graph_result.Graph();

		auto input_result = co_await graph.CreateDeviceInputNodeAsync(MediaCategory::Communications);
		if(input_result.Status() != AudioDeviceNodeCreationStatus::Success)
		{
			PSX_LOGE("Microphone: %s", input_result.Status() == AudioDeviceNodeCreationStatus::AccessDenied
				? "access denied (allow it in the Xbox settings: Privacy & online safety > App privacy > Microphone)"
				: input_result.Status() == AudioDeviceNodeCreationStatus::DeviceNotAvailable
					? "no microphone found (plug a headset into the controller)"
					: "could not open the microphone");
			graph.Close();
			co_return false;
		}

		AudioFrameOutputNode output = graph.CreateFrameOutputNode(graph.EncodingProperties());
		input_result.DeviceInputNode().AddOutgoingConnection(output);
		graph.QuantumStarted([this, output](AudioGraph const &, IInspectable const &) {
			AudioFrame frame = output.GetFrame();
			AudioBuffer buffer = frame.LockBuffer(AudioBufferAccessMode::Read);
			IMemoryBufferReference ref = buffer.CreateReference();
			uint8_t *data = nullptr;
			uint32_t capacity = 0;
			if(FAILED(ref.as<::Windows::Foundation::IMemoryBufferByteAccess>()->GetBuffer(&data, &capacity)) || !data)
				return;
			const size_t samples = buffer.Length() / sizeof(float);
			const float *src = reinterpret_cast<const float *>(data);
			pcm_.resize(samples);
			for(size_t i = 0; i < samples; i++)
				pcm_[i] = static_cast<int16_t>(std::clamp(src[i], -1.0f, 1.0f) * 32767.0f);
			if(cb_ && samples >= 2)
				cb_(pcm_.data(), samples / 2);
		});

		graph.Start();
		graph_ = graph;
		output_ = output;
		PSX_LOGI("Microphone recording");
		co_return true;
	}

	void MicCapture::Stop()
	{
		if(graph_)
		{
			graph_.Stop();
			graph_.Close();
			graph_ = nullptr;
			output_ = nullptr;
			PSX_LOGI("Microphone stopped");
		}
	}
}
