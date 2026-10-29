// The platform layer's sound output when the games are embedded in another
// application (Reprise, src/embed): no device of its own, the stream joins
// the AudioPull of the game thread creating it (platform/audio_pull.hpp) and
// is rendered in step with the game's frames by the embedding host. The rest
// of the standalone platform layer (window, input, file picker) is not part
// of the embedded build.
#include "platform/audio_pull.hpp"
#include "platform/host.hpp"

namespace picarat::platform {

struct AudioStream::Implementation {
    AudioPull* pull = nullptr;
};

AudioStream::AudioStream(int sampleRate, Fill fill) : impl_(std::make_unique<Implementation>()) {
    impl_->pull = AudioPull::current;
    if (impl_->pull) {
        impl_->pull->add(this, sampleRate, std::move(fill));
    }
}

AudioStream::~AudioStream() {
    if (impl_->pull) {
        impl_->pull->remove(this);
    }
}

} // namespace picarat::platform
