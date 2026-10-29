#pragma once
// Movies of the 3DS adventures: the moflex container (sync word "L2" aa ab)
// and the 3DS generation of the Mobiclip video codec, reconstructed from the
// fifth adventure's own code (.code loaded at 0x00100000; the sixth adventure
// carries the same routines at other addresses). Addresses below are the
// fifth adventure's unless noted.
//
// The parts, in the order a frame passes them in the game:
//   Demuxer      - the MoLive reader 0x327c38: sync headers, stream
//                  descriptors, blocks and the bit-packed packet headers,
//                  frames reassembled per stream.
//   VideoDecoder - the hand-written ARM frame routine the player calls
//                  through 0x12b878. The code exists three times, once per
//                  plane stride (1024 at 0x10da00, 256 at 0x114000, 512 at
//                  0x11a600); it is the DS Mobiclip routine with new
//                  partition code tables, a clamped quantizer and a wider
//                  dequantization multiply, so both run on the shared
//                  mobiclip_core.hpp.
//   deblock()    - the post filter 0x165f88 the movie view runs on a copy of
//                  each frame (strength from the frame's quantizer).
//   toRgba()     - the hardware colour converter (Y2R) the movie view feeds
//                  the planes to (0x27bb30): Co and Cg go in as U and V with
//                  the ITU-R BT.601 "scaling" coefficients.
//
// Self-contained (standard library only), like mobiclip.hpp.
#include "video/mobiclip_core.hpp"
#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <span>
#include <stdexcept>
#include <vector>

namespace picarat::moflex {

using mobiclip::DecodeError;

// ---- Container ----

// A stream descriptor from a sync header (factory 0x268174; parsers below).
struct Stream {
    enum class Kind {
        Video,    // types 1 (0x3278e8) and 3 (0x327b04)
        Audio,    // type 2 (0x327840)
        Timeline, // type 4 (0x3279c4): index entries, no decoder
    };
    Kind kind = Kind::Video;
    unsigned type = 0;  // descriptor type in the file
    unsigned index = 0; // stream number used by the packets
    unsigned codec = 0; // second descriptor byte
    // Video: frame rate as a fraction (both eyes of a 3D pair count),
    // picture size, pixel aspect and the frame layout (low nibble of the last
    // type-3 byte; type 1 has none and reports 6 (0x36d318)). The movie view
    // (0x2dd39c) reads layouts 0 and 1 as stereo pairs - 0 left eye first,
    // 1 right eye first - and 2, 3 and 6 as one picture per time.
    std::uint32_t rateNumerator = 0, rateDenominator = 0;
    unsigned width = 0, height = 0, aspectX = 0, aspectY = 0, layout = 6, layoutHigh = 0;
    // Audio (no movie of either 3DS pack has one).
    unsigned sampleRate = 0, channels = 0;
};

// One reassembled frame of a stream: the packets up to and including the
// one that carries the time stamp (0x329cbc).
struct Frame {
    unsigned stream = 0;
    std::int64_t time = 0; // microseconds on the container's clock (first sync: 1)
    unsigned tag = 0;      // the small number before the time stamp (0: key frame)
    std::vector<std::uint8_t> data;
};

class Demuxer {
public:
    explicit Demuxer(std::span<const std::uint8_t> file);

    const std::vector<Stream>& streams() const { return streams_; }

    const Stream* stream(unsigned index) const;
    bool next(Frame& frame); // false at the end of the file
    void rewind();

private:
    struct Assembly {
        unsigned stream;
        std::vector<std::uint8_t> data;
    };

    bool readBlock();
    std::size_t readSync(std::size_t at); // returns the first byte after the descriptors
    std::size_t readPackets(std::size_t at, std::size_t end);

    std::span<const std::uint8_t> file_;
    std::vector<Stream> streams_;
    std::vector<Assembly> partial_;
    std::vector<Frame> ready_;
    std::size_t readyNext_ = 0;
    std::size_t position_ = 0, blockSize_ = 0;
    std::int64_t time_ = 0, step_ = 0; // reader +0x00 and +0x08
    unsigned counter_ = 0x40;          // reader +0x1478: 0x40 means "none yet"
    bool synced_ = false;
};

// ---- Video ----

// The frame routine's constant tables (mobiclip_core.hpp) plus the post
// filter's. They are data in the game's code, so they come from the user's
// ROM: the importer keeps .code as code/code.bin, and fromCode() reads them
// at the addresses of `layout`. Coefficient, coded-block, dequantization and
// scan tables hold the same bytes as the DS decoder's; the partition codes
// are new.
struct VideoTables : mobiclip::CodecTables {
    // Deblocking thresholds for quantizers 12..48 (0x3fa1dc + 12 and + 0x31,
    // read by 0x165f88).
    std::array<std::uint8_t, 37> alpha{}, beta{};

    // Where the tables of one game's code sit.
    struct CodeLayout {
        std::uint32_t decoder;      // first byte of one copy of the frame routine
        std::uint32_t coefficients; // table A; B at +0x2100, clamp at +0x4200 (0x12467c)
        std::uint32_t deblocking;   // alpha/beta tables (0x165f88)
    };

    static constexpr CodeLayout kAdventure5 = {0x11a600, 0x3fa3b4, 0x3fa1dc};
    static constexpr CodeLayout kAdventure6 = {0x16a500, 0x3bfd2c, 0x3bfb54};
    static constexpr std::uint32_t kCodeBase = 0x00100000;

    static std::shared_ptr<const VideoTables> fromCode(std::span<const std::uint8_t> code,
                                                       const CodeLayout& layout);
};

// Decoded planes of one picture: luma and two chroma planes at half
// resolution. The DS decoder called them Co and Cg; the 3DS movie view hands
// the first to the colour converter as U (Cb) and the second as V (Cr).
struct Planes {
    const std::uint8_t* luma = nullptr;
    const std::uint8_t* u = nullptr;
    const std::uint8_t* v = nullptr;
    unsigned stride = 0, width = 0, height = 0;
};

// The player around the shared frame routine (0x12467c, 0x12b878): picture
// buffers without guard rows and their rotation.
class VideoDecoder : mobiclip::FrameDecoder {
public:
    // The player keeps at least six picture buffers (0x36cb70); a frame may
    // refer to the five before it.
    static constexpr unsigned kBuffers = 6;

    VideoDecoder(unsigned width, unsigned height, std::shared_ptr<const VideoTables> tables);

    // 0x12b878 + the frame routine: decodes one frame into the next buffer
    // and returns the byte count the routine reports.
    std::uint32_t decode(std::span<const std::uint8_t> frame);

    Planes planes() const; // the most recent frame

    using FrameDecoder::quantizer;

    unsigned stride() const { return stride_; }

private:
    void checkMotion(const std::uint8_t* from, unsigned width, unsigned height,
                     const std::uint8_t* plane, bool chroma) const override;

    unsigned width_, height_, stride_;
    std::size_t bufferSize_;
    std::vector<std::uint8_t> memory_;
    unsigned next_ = 0, current_ = 0; // player +0x0c: the buffer the next frame goes to
};

// ---- Audio ----

// FastAudio, the movie library's only audio decoder (created by 0x36c3b4,
// run per channel by 0x36c574 through 0x10cff4): each channel's frame is 40
// bytes and yields 256 samples - an 8-coefficient lattice synthesis filter
// driven by four 64-sample pulse trains, then de-emphasis. No movie of
// either 3DS pack has an audio stream; the port is checked against the ROM
// routine on synthetic frames (.analysis/adventure56/tools/fastaudio_oracle).
struct FastAudioTables {
    std::array<std::int32_t, 64> c01{}; // coefficients 0 and 1 (6-bit codes)
    std::array<std::int32_t, 32> c2{}, c3{};
    std::array<std::int32_t, 16> c4{}, c5{};
    std::array<std::int32_t, 8> c6{}, c7{};
    std::array<std::array<std::int32_t, 8>, 64> pulses{}; // pulse amplitude rows

    // `address` is the first table (0x10c5ec in the fifth adventure,
    // 0x15c52c in the sixth); the others follow at fixed distances.
    static constexpr std::uint32_t kAdventure5 = 0x10c5ec, kAdventure6 = 0x15c52c;
    static std::shared_ptr<const FastAudioTables> fromCode(std::span<const std::uint8_t> code,
                                                           std::uint32_t address);
};

class FastAudioDecoder {
public:
    static constexpr std::size_t kFrameBytes = 40;
    static constexpr unsigned kSamples = 256;

    FastAudioDecoder(unsigned channels, std::shared_ptr<const FastAudioTables> tables);

    // One frame per channel (channel frames one after another) into
    // kSamples interleaved samples per channel.
    void decode(std::span<const std::uint8_t> frames, std::int16_t* out);

private:
    struct Channel {
        std::array<std::int32_t, 9> lattice{}; // state +0x190
        std::int32_t emphasis = 0;             // state +0x1b4
    };

    void decodeChannel(const std::uint8_t* frame, std::int16_t* out, Channel& state);

    std::shared_ptr<const FastAudioTables> tables_;
    std::vector<Channel> channels_;
};

// The movie view's second audio decoder (codec 1; 0x36c938 / 0x36ca58):
// 4-bit IMA-style ADPCM. A packet starts with four bytes per channel (step
// index, -, predictor as int16) and continues with 0x80-byte blocks per
// channel, 256 samples each, low nibble first (0x120c08).
struct AdpcmTables {
    std::array<std::int16_t, 89> steps{};
    std::array<std::int8_t, 8> indexChange{};

    static constexpr std::uint32_t kAdventure5 = 0x3fe8cc, kAdventure6 = 0x3c4244;
    static std::shared_ptr<const AdpcmTables> fromCode(std::span<const std::uint8_t> code,
                                                       std::uint32_t address);
};

// Decodes whole audio packets of either codec into interleaved samples, as
// the movie view's 0x36c574 (codec 0) and 0x36ca58 (codec 1) do.
class AudioDecoder {
public:
    AudioDecoder(const Stream& stream, std::shared_ptr<const FastAudioTables> fastAudio,
                 std::shared_ptr<const AdpcmTables> adpcm);

    void decode(std::span<const std::uint8_t> packet, std::vector<std::int16_t>& samples);

private:
    unsigned codec_, channels_;
    std::unique_ptr<FastAudioDecoder> fastAudio_;
    std::shared_ptr<const AdpcmTables> adpcm_;
};

// ---- Display ----

// A picture ready for display (owned, tightly packed planes).
struct Picture {
    unsigned width = 0, height = 0;
    std::vector<std::uint8_t> luma, u, v;
};

// 0x36c678 / 0x165f88: the movie view's post filter, applied to a copy of
// the frame with the frame's quantizer for luma and chroma alike. (The view
// skips it while fewer than six decoded frames wait, 0x2dd620 - a load
// shedding rule; native playback never falls behind and always filters.)
Picture deblock(const Planes& planes, unsigned quantizer, const VideoTables& tables);

// The Y2R conversion (input YUV 4:2:0 planes, output 24-bit RGB, standard
// coefficient set 2) written out as RGBA with alpha 255.
void toRgba(const Picture& picture, std::uint8_t* rgba, std::size_t pitchBytes);

// ---- Convenience ----

// Everything the movie code reads from one game's code.
struct Tables {
    std::shared_ptr<const VideoTables> video;
    std::shared_ptr<const FastAudioTables> fastAudio;
    std::shared_ptr<const AdpcmTables> adpcm;

    enum class Game { Adventure5, Adventure6 };
    static Tables fromCode(std::span<const std::uint8_t> code, Game game);
};

// One movie: demuxes, decodes the video stream into RGBA pictures and any
// audio stream into 16-bit samples.
class Movie {
public:
    Movie(std::vector<std::uint8_t> file, Tables tables);

    const Stream& video() const { return video_; }

    const Stream* audio() const { return hasAudio_ ? &audio_ : nullptr; } // none in either pack

    bool stereo() const { return video_.layout <= 1; }

    // Pictures per eye and their rate (pairs per second for stereo movies).
    unsigned frameCount() const { return frames_; }

    double rate() const;

    // Decodes the next picture into `rgba` (width * height * 4 bytes). `eye`
    // receives 0 for the left and 1 for the right picture of a pair. Returns
    // false at the end. Audio packets met on the way are decoded into the
    // queue takeAudio() empties.
    bool next(std::vector<std::uint8_t>& rgba, unsigned& eye, std::int64_t& time);
    void takeAudio(std::vector<std::int16_t>& samples);
    void rewind();

private:
    std::vector<std::uint8_t> file_;
    Tables tables_;
    std::unique_ptr<Demuxer> demuxer_;
    std::unique_ptr<VideoDecoder> decoder_;
    std::unique_ptr<AudioDecoder> audioDecoder_;
    Stream video_, audio_; // copies: later sync headers may rewrite the demuxer's table
    bool hasAudio_ = false;
    std::vector<std::int16_t> samples_;
    unsigned frames_ = 0, inPair_ = 0;
};

} // namespace picarat::moflex
