// The engine's frame capture request bus (Atom Feature Common, Utils/FrameCaptureBus.h), declared here exactly as
// the engine declares it: that gem's include folder isn't on this gem's include path without a CMake change (a
// reconfigure). Same names, same functions in the same order, so it is the same bus. Only CaptureScreenshot is used.
#pragma once

#include <Atom/RPI.Public/Pass/AttachmentReadback.h>
#include <AzCore/EBus/EBus.h>
#include <AzCore/Outcome/Outcome.h>
#include <AzFramework/Windowing/WindowBus.h>

namespace AZ
{
    namespace Render
    {
        struct FrameCaptureError
        {
            AZ_TYPE_INFO(FrameCaptureError, "{9459AC1D-B0EE-4D89-9EEC-6A65790C76BF}");
            AZStd::string m_errorMessage;
        };

        using FrameCaptureId = uint32_t;
        using FrameCaptureOutcome = AZ::Outcome<FrameCaptureId, FrameCaptureError>;

        class FrameCaptureRequests : public EBusTraits
        {
        public:
            virtual ~FrameCaptureRequests() {};
            virtual bool CanCapture() const = 0;
            virtual FrameCaptureOutcome CaptureScreenshotForWindow(const AZStd::string& imagePath, AzFramework::NativeWindowHandle windowHandle) = 0;
            virtual FrameCaptureOutcome CaptureScreenshot(const AZStd::string& imagePath) = 0;
            virtual FrameCaptureOutcome CaptureScreenshotWithPreview(const AZStd::string& imagePath) = 0;
            virtual FrameCaptureOutcome CapturePassAttachment(const AZStd::string& imagePath, const AZStd::vector<AZStd::string>& passHierarchy,
                const AZStd::string& slotName, RPI::PassAttachmentReadbackOption option) = 0;
            virtual FrameCaptureOutcome CapturePassAttachmentWithCallback(RPI::AttachmentReadback::CallbackFunction callback,
                const AZStd::vector<AZStd::string>& passHierarchy, const AZStd::string& slotName, RPI::PassAttachmentReadbackOption option) = 0;
        };
        using FrameCaptureRequestBus = EBus<FrameCaptureRequests>;
    } // namespace Render
} // namespace AZ
