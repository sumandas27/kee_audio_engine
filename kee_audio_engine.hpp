#pragma once
#include <array>
#include <unordered_map>
#include <fstream>
#include <sstream>
#include <thread>
#include <atomic>
#include <mutex>
#include <AL/al.h>
#include <AL/alc.h>

namespace audio {

class engine {
public:
    static void init();
    /* Engine automatically deinits via destructor.
     * w/o this, engine only inits on the first static call of this engine.
     * use this function to init whenever you'd like
     */

    static float get_volume();
    static void set_volume(float new_volume);
    
    static void play_sfx(const std::string& sfx_file_name);
    static void pause_sfx_mixer();
    static void unpause_sfx_mixer();
    static void stop_sfx_mixer();
    
    static void set_player_music(const std::string& music_file_name, std::size_t index = 0);
    static void unset_player_music(std::size_t index = 0);
    
    static void play_music_player(std::size_t index = 0);
    static void pause_music_player(std::size_t index = 0);
    static bool is_music_playing(std::size_t index = 0);
    
    static float get_music_duration(const std::string& music_file_name);
    static void set_playback_time(float time, std::size_t index = 0);

private:
    using byte = char;
    using wav = std::tuple<int, ALenum, std::size_t, std::size_t, float>;
    /* int          - sample rate
     * ALenum       - format
     * std::size_t  - data start
     * std::size_t  - data size
     * float        - wav duration in seconds
     */

    class sfx_t;
    class sfx_buffers;
    class music_t;
    class music_player {
    public:
        music_player();
        
        void update_buffer_queue(const music_t& music);

        ALuint source_id;
        std::string wav_key;
        std::ifstream music_file;
        std::size_t cursor;
        std::array<ALuint, 4> buffer_ids;
        // empty wav_key means no music file is set to this player
    };
    
    static constexpr std::size_t MUSIC_BUFFER_SIZE = 65536;
    
    static void fetch_al_errors(const std::filesystem::path& file, int line);
    static void fetch_alc_errors(ALCdevice* device, const std::filesystem::path& file, int line);

    static engine& singleton();
    engine();
    ~engine();
    
    void engine_polling_thread();
    std::atomic_bool should_thread_close;
    std::thread polling_thread;
    
    ALCdevice* alc_device;
    ALCcontext* alc_context;
    
    std::unordered_map<std::string, sfx_t> sfx_map;
    std::unordered_map<std::string, music_t> music_map;
    
    std::vector<sfx_buffers> sfx_mixer;
    std::mutex sfx_mixer_lock;
    
    std::array<music_player, 4> music_mixer;
    std::mutex music_mixer_lock;
};

class engine::sfx_t {
public:
    sfx_t(int _sample_rate, ALenum _format, const std::vector<byte>& _data);

    const int sample_rate;
    const ALenum format;
    const std::vector<byte> data;
};

class engine::sfx_buffers {
public:
    sfx_buffers(ALuint _source_id, ALuint _buffer_id);

    ALuint get_source_id() const;
    const ALuint* get_source_id_addr() const;
    const ALuint* get_buffer_id_addr() const;

private:
    ALuint source_id;
    ALuint buffer_id;
};

class engine::music_t {
public:
    music_t(int _sample_rate, ALenum _format, std::size_t _data_start, std::size_t _data_size, float _duration);

    const int sample_rate;
    const ALenum format;
    const std::size_t data_start;
    const std::size_t data_size;
    const float duration;
    const bool is_duo_byte_sampled;
};

} // namespace audio
