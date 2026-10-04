#include <gyrolib/gyrolib.h>
#include <cstring>
#include <cstdio>

int main() {
    gl_context* context=gl_create(GL_ABI_VERSION);
    if(!context)return 1;
    bool ok=std::strcmp(gl_text(nullptr,"GyroEnabled"),"GYROSCOPE")==0;
    // Cover the first/last language, mixed-case keys, punctuation, UTF-8,
    // absent keys and null input. Sorting must be ordinal on every build host.
    struct Translation {const char *language,*key,*expected;};
    const Translation cases[]={
        {"en","SensitivityX","SENSITIVITY X"},
        {"fr","SensitivityX","SENSIBILITÉ X"},
        {"de","GyroEnabled","GYROSKOP"},
        {"pt","GyroEnabled","GIROSCÓPIO"},
        {"pt","InvertYaw","INVERTER GUINADA"},
        {"de","InvertRoll","ROLLEN INVERTIEREN"},
        {"es","GyroEnabled","GIROSCOPIO"},
        {"it","GyroEnabled","GIROSCOPIO"},
        {"en","does.not.exist","?"},
        {"fr","","?"},
        {"en",nullptr,"?"}
    };
    const Translation catalog[]={
#include "localization_catalog.inc"
    };
    for(const auto& test:catalog){
        if(gl_set_language(context,test.language)!=GL_OK||std::strcmp(gl_text(context,test.key),test.expected)!=0){
            std::fprintf(stderr,"Catalog mismatch: %s/%s\n",test.language,test.key);ok=false;
        }
    }
    for(const auto& test:cases){
        if(gl_set_language(context,test.language)!=GL_OK ||
           std::strcmp(gl_text(context,test.key),test.expected)!=0){
            std::fprintf(stderr,"Translation failed: %s/%s\n",test.language,test.key?test.key:"(null)");
            ok=false;
        }
    }
    gl_destroy(context);
    return ok?0:1;
}
