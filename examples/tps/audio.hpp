#pragma once
#include "host.hpp"
#include <SDL3/SDL.h>
#include <vector>

namespace tps {
// Procedural sounds belong only to this example host, never to GyroLib's core.
// Short, bounded clips need no callback or shared gameplay state on an audio thread.
class Audio {
    SDL_AudioStream* stream{};
    bool initialized{},was_reloading{};
    unsigned seen_shots{},seen_reloads{},seen_pumps{};
    uint32_t noise=0x8726319u;
    void play(int kind,int weapon=Rifle,bool hit=false){
        if(!stream)return;
        if(SDL_GetAudioStreamQueued(stream)>48000*int(sizeof(float))/2)SDL_ClearAudioStream(stream);
        constexpr int rate=48000;
        const double durations[]={.115,.19,.095,.23},frequencies[]={128,82,185,65};
        const double noise_decay[]={.013,.021,.010,.031},tone_decay[]={.028,.047,.023,.055};
        const double duration=kind==0?durations[weapon]:.10;
        std::vector<float> samples(size_t(duration*rate));
        for(size_t i=0;i<samples.size();++i){
            const double t=double(i)/rate;
            noise^=noise<<13;noise^=noise>>17;noise^=noise<<5;
            const double n=double(noise)/double(UINT32_MAX)*2-1;
            double sample=0;
            if(kind==0){
                sample=.29*n*std::exp(-t/noise_decay[weapon])+
                    .20*std::sin(2*std::numbers::pi*frequencies[weapon]*t)*std::exp(-t/tone_decay[weapon]);
                if(hit&&t>.035)sample+=.04*std::sin(2*std::numbers::pi*1550*t)*std::exp(-(t-.035)/.011);
            }else{
                sample=.14*n*std::exp(-t/.008);
                if((kind==1||kind==3)&&t>.055)sample+=.10*n*std::exp(-(t-.055)/.009);
            }
            // Avoid a discontinuity at the start of the procedural impulse.
            samples[i]=float(sample*std::min(1.0,t/.0004));
        }
        SDL_PutAudioStreamData(stream,samples.data(),int(samples.size()*sizeof(float)));
        SDL_FlushAudioStream(stream);
    }
public:
    Audio()=default;Audio(const Audio&)=delete;Audio& operator=(const Audio&)=delete;
    ~Audio(){close();}
    void open(){
        initialized=SDL_InitSubSystem(SDL_INIT_AUDIO);
        if(!initialized)return;
        const SDL_AudioSpec spec{SDL_AUDIO_F32,1,48000};
        stream=SDL_OpenAudioDeviceStream(SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK,&spec,nullptr,nullptr);
        if(stream)SDL_ResumeAudioStreamDevice(stream);
    }
    void close(){
        if(stream){SDL_DestroyAudioStream(stream);stream=nullptr;}
        if(initialized){SDL_QuitSubSystem(SDL_INIT_AUDIO);initialized=false;}
    }
    void update(const Host& host,bool active){
        if(active){
            if(host.shots!=seen_shots)play(0,host.equipped,host.last_hit_zone>=0);
            if(host.pump_pose()>0&&seen_pumps!=host.shots){play(3);seen_pumps=host.shots;}
            if(host.reloads!=seen_reloads)play(1);
            if(was_reloading&&!host.reloading()&&host.ammo()==host.weapon().capacity)play(2);
        }else if(stream)SDL_ClearAudioStream(stream);
        seen_shots=host.shots;seen_reloads=host.reloads;was_reloading=host.reloading();
    }
};
}
