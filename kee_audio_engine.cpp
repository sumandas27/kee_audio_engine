#include "audio.hpp"
#include <bit>

#define CHECK_AL_ERRORS()\
    fetch_al_errors(__FILE__, __LINE__)
    
#define CHECK_ALC_ERRORS(device)\
    fetch_alc_errors(device, __FILE__, __LINE__)

namespace audio {

// ------------------------------------------------------------------- //
// ENGINE

void engine::init() {
    singleton();
}

float engine::get_volume() {
    float volume;
    alGetListenerf(AL_GAIN, &volume); CHECK_AL_ERRORS();
    return volume;
}

void engine::set_volume(float new_volume) {
    if (new_volume < 0.0f || new_volume > 1.0f)
        throw std::out_of_range("engine::set_volume: Volume must be between 0.0 and 1.0");

    alListenerf(AL_GAIN, new_volume); CHECK_AL_ERRORS();
}

void engine::play_sfx(const std::string& sfx_file_name) {
    const sfx_t& sfx = singleton().sfx_map.at(sfx_file_name);

    ALuint source_id;
    alGenSources(1, &source_id); CHECK_AL_ERRORS();
    
    alSourcef(source_id, AL_PITCH, 1); CHECK_AL_ERRORS();
    alSourcef(source_id, AL_GAIN, 1.0f); CHECK_AL_ERRORS();
    alSource3f(source_id, AL_POSITION, 0.0f, 0.0f, 0.0f); CHECK_AL_ERRORS();
    alSourcei(source_id, AL_LOOPING, AL_FALSE); CHECK_AL_ERRORS();
    
    ALuint buffer_id;
    alGenBuffers(1, &buffer_id); CHECK_AL_ERRORS();
    alBufferData(buffer_id, sfx.format, sfx.data.data(), static_cast<ALsizei>(sfx.data.size()), sfx.sample_rate); CHECK_AL_ERRORS();
    alSourcei(source_id, AL_BUFFER, buffer_id); CHECK_AL_ERRORS();
    
    alSourcePlay(source_id); CHECK_AL_ERRORS();
    
    singleton().sfx_mixer_lock.lock();
    singleton().sfx_mixer.emplace_back(source_id, buffer_id);
    singleton().sfx_mixer_lock.unlock();
}

void engine::pause_sfx_mixer() {
    singleton().sfx_mixer_lock.lock();
    for (const sfx_buffers& sfx : singleton().sfx_mixer)
        alSourcePause(sfx.get_source_id());
    singleton().sfx_mixer_lock.unlock();
    CHECK_AL_ERRORS();
}

void engine::unpause_sfx_mixer() {
    singleton().sfx_mixer_lock.lock();
    for (const sfx_buffers& sfx : singleton().sfx_mixer)
        alSourcePlay(sfx.get_source_id());
    singleton().sfx_mixer_lock.unlock();
    CHECK_AL_ERRORS();
}

void engine::stop_sfx_mixer() {
    singleton().sfx_mixer_lock.lock();
    for (const sfx_buffers& sfx : singleton().sfx_mixer)
        alSourceStop(sfx.get_source_id());
    singleton().sfx_mixer_lock.unlock();
    CHECK_AL_ERRORS();
}

void engine::set_player_music(const std::string& music_file_name, std::size_t index) {
    singleton().music_mixer_lock.lock();
    music_player& player = singleton().music_mixer.at(index);
    
    player.wav_key = music_file_name;
    player.music_file = std::ifstream("assets/music/" + music_file_name);
    player.cursor = 0;
    
    const music_t& music = singleton().music_map.at(music_file_name);
    player.update_buffer_queue(music);
    singleton().music_mixer_lock.unlock();
}

void engine::unset_player_music(std::size_t index) {
    music_player& player = singleton().music_mixer.at(index);
    
    alSourcei(player.source_id, AL_BUFFER, NULL); CHECK_AL_ERRORS();
    player.wav_key = "";
    player.music_file = std::ifstream();
    player.cursor = 0;
}

void engine::play_music_player(std::size_t index) {
    singleton().music_mixer_lock.lock();
    if (singleton().music_mixer.at(index).wav_key.empty())
        throw std::logic_error("audio::engine::play_music_player: music player has no music set (use audio::engine::set_player_music)");
    
    alSourcePlay(singleton().music_mixer.at(index).source_id); CHECK_AL_ERRORS();
    singleton().music_mixer_lock.unlock();
}

void engine::pause_music_player(std::size_t index) {
    singleton().music_mixer_lock.lock();
    if (singleton().music_mixer.at(index).wav_key.empty())
        throw std::logic_error("audio::engine::play_music_player: music player has no music set (use audio::engine::set_player_music)");
    
    alSourcePause(singleton().music_mixer.at(index).source_id); CHECK_AL_ERRORS();
    singleton().music_mixer_lock.unlock();
}

bool engine::is_music_playing(std::size_t index) {
    ALint source_state;
    singleton().music_mixer_lock.lock();
    alGetSourcei(singleton().music_mixer.at(index).source_id, AL_SOURCE_STATE, &source_state); CHECK_AL_ERRORS();
    singleton().music_mixer_lock.unlock();
    return source_state == AL_PLAYING;
}

float engine::get_music_duration(const std::string& music_file_name) {
    singleton().music_mixer_lock.lock();
    float res = singleton().music_map.at(music_file_name).duration;
    singleton().music_mixer_lock.unlock();
    return res;
}

void engine::set_playback_time(float time, std::size_t index) {
    singleton().music_mixer_lock.lock();
    music_player& player = singleton().music_mixer.at(index);
    if (player.wav_key.empty())
        throw std::logic_error("audio::engine::set_playback_time: music player has no music set (use audio::engine::set_player_music)");
        
    const music_t& music = singleton().music_map.at(player.wav_key);
    if (time <= 0.0f)
        player.cursor = 0;
    else if (time >= music.duration)
        player.cursor = music.data_size;
    else {
        float playback_percent = time / music.duration;
        player.cursor = playback_percent * music.data_size;
        if (music.is_duo_byte_sampled)
            player.cursor -= player.cursor % 2;
    }
    
    player.update_buffer_queue(music);
    singleton().music_mixer_lock.unlock();
}

//--- ENGINE::MUSIC_PLAYER ---//

engine::music_player::music_player() { }

void engine::music_player::update_buffer_queue(const music_t& music) {
    alSourceStop(source_id); CHECK_AL_ERRORS();
    alSourcei(source_id, AL_BUFFER, NULL); CHECK_AL_ERRORS();
    ALint buffer_count = 0;
    while (buffer_count < buffer_ids.size()) {
        ALsizei buffer_size = static_cast<ALsizei>(std::min(MUSIC_BUFFER_SIZE, music.data_size - cursor));
        buffer_size -= buffer_size % 8;
        if (buffer_size == 0)
            break;
        
        std::vector<byte> buffer_data(buffer_size);
        music_file.seekg(music.data_start + cursor);
        music_file.read(buffer_data.data(), buffer_size);
        alBufferData(buffer_ids[buffer_count], music.format, buffer_data.data(), buffer_size, music.sample_rate); CHECK_AL_ERRORS();
        buffer_count++;
        
        cursor += buffer_size;
        if (buffer_size < MUSIC_BUFFER_SIZE) {
            cursor = music.data_size;
            break;
        }
    }
    alSourceQueueBuffers(source_id, buffer_count, buffer_ids.data()); CHECK_AL_ERRORS();
}

//----------------------------//

void engine::fetch_al_errors(const std::filesystem::path& file, int line) {
    bool error_found = false;
    std::stringstream err_msg_stream;
    
    ALenum error_flag = alGetError();
    while (error_flag != AL_NO_ERROR) {
        if (!error_found) {
            error_found = true;
            err_msg_stream << "OpenAL Flags: ";
        }
        
        switch (error_flag) {
        case AL_INVALID_NAME:
            err_msg_stream << "\"AL_INVALID_NAME\" ";
            break;
        case AL_INVALID_ENUM:
            err_msg_stream << "\"AL_INVALID_ENUM\" ";
            break;
        case AL_INVALID_VALUE:
            err_msg_stream << "\"AL_INVALID_VALUE\" ";
            break;
        case AL_INVALID_OPERATION:
            err_msg_stream << "\"AL_INVALID_OPERATION\" ";
            break;
        case AL_OUT_OF_MEMORY:
            err_msg_stream << "\"AL_OUT_OF_MEMORY\" ";
            break;
        default:
            err_msg_stream << "\"Flag not deducable\" ";
            break;
        }
        
        error_flag = alGetError();
    }
    
    if (error_found) {
        err_msg_stream << "File " << file << " @ Line " << line;
        throw std::runtime_error(err_msg_stream.str());
    }
}

void engine::fetch_alc_errors(ALCdevice* device, const std::filesystem::path& file, int line) {
    bool error_found = false;
    std::stringstream err_msg_stream;
    
    ALCenum error_flag = alcGetError(device);
    while (error_flag != ALC_NO_ERROR) {
        if (!error_found) {
            error_found = true;
            err_msg_stream << "OpenAL Flags: ";
        }
        
        switch (error_flag) {
        case ALC_INVALID_DEVICE:
            err_msg_stream << "\"ALC_INVALID_DEVICE\" ";
            break;
        case ALC_INVALID_CONTEXT:
            err_msg_stream << "\"ALC_INVALID_CONTEXT\" ";
            break;
        case ALC_INVALID_ENUM:
            err_msg_stream << "\"ALC_INVALID_ENUM\" ";
            break;
        case ALC_INVALID_VALUE:
            err_msg_stream << "\"ALC_INVALID_VALUE\" ";
            break;
        case ALC_OUT_OF_MEMORY:
            err_msg_stream << "\"ALC_OUT_OF_MEMORY\" ";
            break;
        default:
            err_msg_stream << "\"Flag not deducable\" ";
            break;
        }
        
        error_flag = alcGetError(device);
    }
    
    if (error_found) {
        err_msg_stream << "File " << file << " @ Line " << line;
        throw std::runtime_error(err_msg_stream.str());
    }
}

engine& engine::singleton() {
    static engine singleton = engine();
    return singleton;
}

engine::engine() {
    alc_device = alcOpenDevice(nullptr); CHECK_ALC_ERRORS(alc_device);
    if (alc_device == nullptr)
        throw std::ios_base::failure("alcOpenDevice: Unable to create OpenAL device");
        
    alc_context = alcCreateContext(alc_device, nullptr); CHECK_ALC_ERRORS(alc_device);
    if (alc_context == nullptr)
        throw std::ios_base::failure("alcCreateContext: Unable to create OpenAL context");
        
    if (!alcMakeContextCurrent(alc_context))
        throw std::ios_base::failure("alcMakeCurrentContext: Could not set OpenAL context to current context");
    CHECK_ALC_ERRORS(alc_device);
    
    alListenerf(AL_GAIN, 0.25f); CHECK_AL_ERRORS();
    alListener3f(AL_POSITION, 0.0f, 0.0f, 0.0f); CHECK_AL_ERRORS();
    
    static const auto open_wav = [](const std::string& file_name, const std::string& full_path) -> std::ifstream {
        if (file_name.substr(file_name.size() - 4) != ".wav")
            throw std::logic_error("audio::engine::open_wav: attempted opening a non .wav file");
    
        std::ifstream wav_file(full_path, std::ios::binary);
        if (!wav_file.is_open())
            throw std::filesystem::filesystem_error("audio::engine::open_wav: Could not open wav file at" + full_path, std::error_code());
            
        return wav_file;
    };
    
    static const auto load_wav = [](std::ifstream& wav_file) -> wav {
        static const auto buffer_to_number = [](byte* buffer, std::size_t len) -> std::int32_t {
            if (len > 4)
                throw std::logic_error("audio::engine::buffer_to_number: Buffer can only contain up at 4 bytes");
            
            if constexpr (std::endian::native == std::endian::big)
                std::reverse(buffer, buffer + len);
            
            std::int32_t res = 0;
            std::memcpy(&res, buffer, len);
            return res;
        };
            
        std::array<char, 4> wav_header_buffer;
        
        if (!wav_file.read(wav_header_buffer.data(), 4))
            throw std::filesystem::filesystem_error("audio::engine::load_wav: Could not read RIFF", std::error_code());
        if (std::strncmp(wav_header_buffer.data(), "RIFF", 4) != 0)
            throw std::filesystem::filesystem_error("audio::engine::load_wav: wav header does not contain RIFF", std::error_code());
            
        if (!wav_file.read(wav_header_buffer.data(), 4))
            throw std::filesystem::filesystem_error("audio::engine::load_wav: Could not read size of wav file", std::error_code());
            
        if (!wav_file.read(wav_header_buffer.data(), 4))
            throw std::filesystem::filesystem_error("audio::engine::load_wav: Could not read WAVE", std::error_code());
        if (std::strncmp(wav_header_buffer.data(), "WAVE", 4) != 0)
            throw std::filesystem::filesystem_error("audio::engine::load_wav: wav header does not contain WAVE", std::error_code());
        
        int num_channels = 0;
        int sample_rate = 0;
        int bits_per_sample = 0;
        ALenum format = AL_NONE;
        std::vector<byte> wav_data;
        
        while (wav_file.good() && wav_file.peek() != EOF) {
            if (!wav_file.read(wav_header_buffer.data(), 4))
                throw std::filesystem::filesystem_error("audio::engine::load_wav: Could not read chunk ID", std::error_code());
            
            if (std::strncmp(wav_header_buffer.data(), "fmt ", 4) == 0) {
                if (!wav_file.read(wav_header_buffer.data(), 4))
                    throw std::filesystem::filesystem_error("audio::engine::load_wav: Could not read chunk size", std::error_code());
                
                if (!wav_file.read(wav_header_buffer.data(), 2))
                    throw std::filesystem::filesystem_error("audio::engine::load_wav: Could not read audio format", std::error_code());
                
                if (!wav_file.read(wav_header_buffer.data(), 2))
                    throw std::filesystem::filesystem_error("audio::engine::load_wav: Could not read number of channels", std::error_code());
                num_channels = buffer_to_number(wav_header_buffer.data(), 2);
                
                if (!wav_file.read(wav_header_buffer.data(), 4))
                    throw std::filesystem::filesystem_error("audio::engine::load_wav: Could not read sample rate", std::error_code());
                sample_rate = buffer_to_number(wav_header_buffer.data(), 4);
                
                if (!wav_file.read(wav_header_buffer.data(), 4))
                    throw std::filesystem::filesystem_error("audio::engine::load_wav: Could not read byte rate", std::error_code());
                    
                if (!wav_file.read(wav_header_buffer.data(), 2))
                    throw std::filesystem::filesystem_error("audio::engine::load_wav: Could not read block align", std::error_code());
                    
                if (!wav_file.read(wav_header_buffer.data(), 2))
                    throw std::filesystem::filesystem_error("audio::engine::load_wav: Could not read bits per sample", std::error_code());
                bits_per_sample = buffer_to_number(wav_header_buffer.data(), 2);
                
                if (num_channels == 1 && bits_per_sample == 8)
                    format = AL_FORMAT_MONO8;
                else if (num_channels == 1 && bits_per_sample == 16)
                    format = AL_FORMAT_MONO16;
                else if (num_channels == 2 && bits_per_sample == 8)
                    format = AL_FORMAT_STEREO8;
                else if (num_channels == 2 && bits_per_sample == 16)
                    format = AL_FORMAT_STEREO16;
                else
                    throw std::filesystem::filesystem_error("audio::engine::load_wav: Invalid filesystem format", std::error_code());
            }
            else if (std::strncmp(wav_header_buffer.data(), "data", 4) == 0) {
                if (!wav_file.read(wav_header_buffer.data(), 4))
                    throw std::filesystem::filesystem_error("audio::engine::load_wav: Could not read wav size", std::error_code());
                    
                std::size_t data_start = wav_file.tellg();
                std::size_t data_size = buffer_to_number(wav_header_buffer.data(), 4);
                
                float num_samples = data_size / (num_channels * bits_per_sample / 8);
                float duration = num_samples / sample_rate;
                return std::make_tuple(sample_rate, format, data_start, data_size, duration);
            }
            else {
                if (!wav_file.read(wav_header_buffer.data(), 4))
                    throw std::filesystem::filesystem_error("audio::engine::load_wav: Could not read LIST chunk size", std::error_code());
                    
                ALsizei list_chunk_size = buffer_to_number(wav_header_buffer.data(), 4);
                wav_file.ignore(list_chunk_size);
            }
        }
        
        throw std::filesystem::filesystem_error("audio::engine::load_wav: wav file stream went bad or could not find data chunk", std::error_code());
    };

    for (const std::filesystem::directory_entry& full_path : std::filesystem::directory_iterator("assets/sfx/")) {
        std::string file_name = full_path.path().string().substr(directory.string().size());
        if (file_name[0] == '/')
            file_name.erase(0, 1);
        
        if (file_name == ".DS_Store")
            continue;
            
        if (music_map.find(file_name) != music_map.end())
            throw std::logic_error("audio::engine::load_wav: music file name already exists");
    
        std::ifstream music_file = open_wav(file_name, full_path.path().string());
        const auto [sample_rate, format, data_start, data_size, duration] = load_wav(music_file);
        music_map.try_emplace(file_name, sample_rate, format, data_start, data_size, duration);
    }
    
    for (const std::filesystem::directory_entry& full_path : std::filesystem::directory_iterator("assets/music/")) {
        std::string file_name = full_path.path().string().substr(directory.string().size());
        if (file_name[0] == '/')
            file_name.erase(0, 1);
        
        if (file_name == ".DS_Store")
            continue;
            
        if (music_map.find(file_name) != music_map.end())
            throw std::logic_error("audio::engine::load_wav: music file name already exists");
    
        std::ifstream music_file = open_wav(file_name, full_path.path().string());
        const auto [sample_rate, format, data_start, data_size, duration] = load_wav(music_file);
        music_map.try_emplace(file_name, sample_rate, format, data_start, data_size, duration);
    }
    
    for (music_player& player : music_mixer) {
        alGenSources(1, &player.source_id); CHECK_AL_ERRORS();
        alSourcef(player.source_id, AL_PITCH, 1); CHECK_AL_ERRORS();
        alSourcef(player.source_id, AL_GAIN, 1.0f); CHECK_AL_ERRORS();
        alSource3f(player.source_id, AL_POSITION, 0.0f, 0.0f, 0.0f); CHECK_AL_ERRORS();
        alSourcei(player.source_id, AL_LOOPING, AL_FALSE); CHECK_AL_ERRORS();
        
        alGenBuffers(4, player.buffer_ids.data());
    }
    
    should_thread_close = false;
    polling_thread = std::thread(&engine::engine_polling_thread, this);
}

engine::~engine() {
    sfx_mixer_lock.unlock();
    music_mixer_lock.unlock();
    should_thread_close = true;
    polling_thread.join();
    
    for (const sfx_buffers& sfx : sfx_mixer) {
        alSourceStop(sfx.get_source_id()); CHECK_AL_ERRORS();
        alDeleteSources(1, sfx.get_source_id_addr()); CHECK_AL_ERRORS();
        alDeleteBuffers(1, sfx.get_buffer_id_addr()); CHECK_AL_ERRORS();
    }
    
    for (music_player& player : music_mixer) {
        alSourceStop(player.source_id); CHECK_AL_ERRORS();
        alSourcei(player.source_id, AL_BUFFER, NULL); CHECK_AL_ERRORS();
        alDeleteSources(1, &player.source_id); CHECK_AL_ERRORS();
        alDeleteBuffers(4, player.buffer_ids.data()); CHECK_AL_ERRORS();
    }
    
    alcMakeContextCurrent(nullptr); CHECK_ALC_ERRORS(alc_device);
    alcDestroyContext(alc_context); CHECK_ALC_ERRORS(alc_device);
    alcCloseDevice(alc_device); CHECK_ALC_ERRORS(alc_device);
}

void engine::engine_polling_thread() {
    while (!should_thread_close) {
        sfx_mixer_lock.lock();
        std::erase_if(sfx_mixer, [](const sfx_buffers& sfx) -> bool {
            ALint source_state = AL_PLAYING;
            alGetSourcei(sfx.get_source_id(), AL_SOURCE_STATE, &source_state); CHECK_AL_ERRORS();
            if (source_state != AL_STOPPED)
                return false;
                
            alDeleteSources(1, sfx.get_source_id_addr()); CHECK_AL_ERRORS();
            alDeleteBuffers(1, sfx.get_buffer_id_addr()); CHECK_AL_ERRORS();
            return true;
        });
        sfx_mixer_lock.unlock();
        
        music_mixer_lock.lock();
        for (music_player& player : music_mixer) {
            ALint source_state = AL_NONE;
            alGetSourcei(player.source_id, AL_SOURCE_STATE, &source_state); CHECK_AL_ERRORS();
            
            if (source_state != AL_PLAYING)
                continue;
              
            ALint buffers_processed;
            alGetSourcei(player.source_id, AL_BUFFERS_PROCESSED, &buffers_processed); CHECK_AL_ERRORS();
            while (buffers_processed > 0) {
                buffers_processed--;
            
                ALuint buffer;
                alSourceUnqueueBuffers(player.source_id, 1, &buffer); CHECK_AL_ERRORS();
                
                const music_t& music = singleton().music_map.at(player.wav_key);
                if (player.cursor >= music.data_size)
                    continue;
                    
                ALsizei buffer_size = static_cast<ALsizei>(std::min(MUSIC_BUFFER_SIZE, music.data_size - player.cursor));
                buffer_size -= buffer_size % 8;
                
                std::vector<byte> buffer_data(buffer_size);
                player.music_file.seekg(music.data_start + player.cursor);
                player.music_file.read(buffer_data.data(), buffer_size);
                
                alBufferData(buffer, music.format, buffer_data.data(), buffer_size, music.sample_rate); CHECK_AL_ERRORS();
                alSourceQueueBuffers(player.source_id, 1, &buffer); CHECK_AL_ERRORS();
                
                player.cursor += buffer_size;
                if (buffer_size < MUSIC_BUFFER_SIZE)
                    player.cursor = music.data_size;
            }
        }
        music_mixer_lock.unlock();
        
        static constexpr int UPDATES_PER_SECOND = 200;
        static constexpr int UPDATE_FRAME_MS = 1000 / UPDATES_PER_SECOND;
        std::this_thread::sleep_for(std::chrono::milliseconds(UPDATE_FRAME_MS));
    }
}

// ------------------------------------------------------------------- //
// ENGINE::SFX_T

engine::sfx_t::sfx_t(int _sample_rate, ALenum _format, const std::vector<byte>& _data) :
    sample_rate(_sample_rate),
    format(_format),
    data(_data)
{ }

// ------------------------------------------------------------------- //
// ENGINE::SFX_BUFFERS

engine::sfx_buffers::sfx_buffers(ALuint _source_id, ALuint _buffer_id) :
    source_id(_source_id),
    buffer_id(_buffer_id)
{ }

ALuint engine::sfx_buffers::get_source_id() const {
    return source_id;
}

const ALuint* engine::sfx_buffers::get_source_id_addr() const {
    return &source_id;
}

const ALuint* engine::sfx_buffers::get_buffer_id_addr() const {
    return &buffer_id;
}

// ------------------------------------------------------------------- //
// ENGINE::MUSIC_T

engine::music_t::music_t(int _sample_rate, ALenum _format, std::size_t _data_start, std::size_t _data_size, float _duration) :
    sample_rate(_sample_rate),
    format(_format),
    data_start(_data_start),
    data_size(_data_size),
    duration(_duration),
    is_duo_byte_sampled(format == AL_FORMAT_MONO16 || format == AL_FORMAT_STEREO16)
{ }

} // namespace audio
