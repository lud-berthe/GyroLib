#pragma once
#include <algorithm>
#include <array>
#include <chrono>
#include <fstream>
#include <filesystem>
#include <vector>
#include <numeric>
namespace tps {
inline uint64_t performance_clock(){return std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now().time_since_epoch()).count();}
class Performance {
    std::ofstream file;
    std::array<std::vector<double>,9> samples;
    unsigned reports{};
public:
    enum Stage {Events,Acquisition,Core,Host,Scene,UI,Present,Frame,GPU};
    bool active()const{return file.is_open()&&reports<600;}
    void open(const char* path){file.open(std::filesystem::path(reinterpret_cast<const char8_t*>(path)));if(file)file<<"scenario,stage,frames,mean_ms,p95_ms,p99_ms,max_ms\n";}
    void add(Stage stage,uint64_t ns){if(active())samples[stage].push_back(ns*1e-6);}
    void report(const char* scenario){if(!active())return;
        constexpr const char* names[]={"events","acquisition","gyrolib_update","host_with_gyro","scene","ui","present","frame","scene_gpu"};
        for(unsigned i=0;i<samples.size();++i){auto& v=samples[i];if(v.empty())continue;
            const auto mean=std::accumulate(v.begin(),v.end(),0.)/v.size();std::sort(v.begin(),v.end());
            file<<scenario<<','<<names[i]<<','<<v.size()<<','<<mean<<','<<v[size_t((v.size()-1)*.95)]<<','<<v[size_t((v.size()-1)*.99)]<<','<<v.back()<<'\n';v.clear();}
        file.flush();++reports;
    }
};
}
