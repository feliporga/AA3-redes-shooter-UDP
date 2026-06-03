#pragma once
#include <SFML/Audio.hpp>
#include <unordered_map>
#include <string>
#include <vector>
#include <cassert>
#include <iostream>

#define AM AudioManager::GetInstance()

class AudioManager
{
private:
    std::unordered_map<std::string, sf::SoundBuffer*> _clips;
    std::unordered_map<std::string, sf::Music*> _songs;
    std::vector<sf::Sound*> _channels;

    bool _muted = false;

    // volume
    float _musicVolume = 20.0f;
    float _sfxVolume = 5.0f;

    std::string _currentSongName = "";

    AudioManager() {}

    ~AudioManager()
    {
        for (sf::Sound* channel : _channels) delete channel;
        for (const std::pair<const std::string, sf::SoundBuffer*>& clip : _clips) delete clip.second;
        for (const std::pair<const std::string, sf::Music*>& song : _songs) delete song.second;
        _channels.clear();
        _clips.clear();
        _songs.clear();
    }

    AudioManager(const AudioManager&) = delete;
    AudioManager& operator=(const AudioManager&) = delete;

public:
    inline static AudioManager& GetInstance()
    {
        static AudioManager am;
        return am;
    }

    inline void LoadClip(std::string name)
    {
        if (_clips.find(name) != _clips.end()) return;

        sf::SoundBuffer* loadedChunk = new sf::SoundBuffer();
        if (!loadedChunk->loadFromFile("resources/audio/sfx/" + name + ".wav")) {
            std::cerr << "Error cargando SFX: " << name << std::endl;
            delete loadedChunk;
            return;
        }

        _clips.emplace(name, loadedChunk);
    }

    inline void LoadSong(std::string name)
    {
        if (_songs.find(name) != _songs.end()) return;

        sf::Music* loadedMusic = new sf::Music();
        if (!loadedMusic->openFromFile("resources/audio/music/" + name + ".wav")) {
            std::cerr << "Error cargando Musica: " << name << std::endl;
            delete loadedMusic;
            return;
        }

        _songs.emplace(name, loadedMusic);
    }

    inline void PlayClip(std::string name, short repetitions)
    {
        if (_muted || _clips.find(name) == _clips.end()) return;

        for (sf::Sound* channel : _channels) {
            if (channel->getStatus() == sf::Sound::Status::Stopped) {
                channel->setBuffer(*_clips[name]);
                channel->setLooping(repetitions == -1);
                channel->setVolume(_sfxVolume);
                channel->play();
                return;
            }
        }

        if (_channels.size() < 32) {
            sf::Sound* newSound = new sf::Sound(*_clips[name]);
            newSound->setLooping(repetitions == -1);
            newSound->setVolume(_sfxVolume);
            newSound->play();
            _channels.push_back(newSound);
        }
    }

    inline void PlaySong(std::string name)
    {
        if (_muted) return;

        if (_songs.find(name) == _songs.end()) {
            LoadSong(name);
            if (_songs.find(name) == _songs.end()) return;
        }

        if (_currentSongName == name) {
            if (_songs[name]->getStatus() != sf::Sound::Status::Playing) {
                _songs[name]->play();
            }
            return;
        }

        if (_currentSongName != "" && _songs.find(_currentSongName) != _songs.end()) {
            _songs[_currentSongName]->stop();
        }

        _currentSongName = name;
        _songs[name]->setLooping(true);
        _songs[name]->setVolume(_musicVolume);
        _songs[name]->play();
    }


    inline void SetMusicVolume(float volume)
    {
        _musicVolume = volume;
        for (const std::pair<const std::string, sf::Music*>& song : _songs) {
            song.second->setVolume(_musicVolume);
        }
    }

    inline void SetSFXVolume(float volume)
    {
        _sfxVolume = volume;
        for (sf::Sound* channel : _channels) {
            channel->setVolume(_sfxVolume);
        }
    }

    inline void Mute()
    {
        _muted = true;
        StopAudio();
    }

    inline void UnMute()
    {
        _muted = false;
    }

    inline bool GetMuted() { return _muted; }

    inline void StopAudio()
    {
        for (sf::Sound* channel : _channels) {
            channel->stop();
        }
        for (const std::pair<const std::string, sf::Music*>& song : _songs) {
            song.second->stop();
        }
    }
};