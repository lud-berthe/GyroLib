#Requires -Version 7.0
$ErrorActionPreference = 'Stop'
$projectRoot = Split-Path $PSScriptRoot -Parent
$lines = [System.Collections.Generic.List[string]]::new()
$lines.Add('// Generated from localization/*.json. Run tools/generate_localization.ps1 after editing.')
$lines.Add('#include <algorithm>')
$lines.Add('#include <cstring>')
$lines.Add('#include <iterator>')
$lines.Add('#include <string_view>')
$lines.Add('namespace gyrolib {')
$lines.Add('struct Text { const char *language,*key,*value; };')
$lines.Add('static constexpr Text texts[] = {')
# Match the bytewise C++ lookup order; culture-sensitive sorting is incorrect
# for mixed-case keys and punctuation.
foreach($lang in @('de','en','es','fr','it','pt')) {
    $data = Get-Content (Join-Path $projectRoot "localization/$lang.json") -Raw -Encoding utf8 | ConvertFrom-Json -AsHashtable
    $keys = [string[]]@($data.Keys)
    [Array]::Sort($keys, [StringComparer]::Ordinal)
    foreach($key in $keys) {
        $k = ConvertTo-Json -InputObject ([string]$key) -Compress
        $v = ConvertTo-Json -InputObject ([string]$data[$key]) -Compress
        $lines.Add('{"' + $lang + '",' + $k + ',' + $v + '},')
    }
}
$lines.Add('};')
$lines.Add('static const char* find_text(std::string_view language,const char* key) {')
$lines.Add('    const Text wanted{language.data(),key,nullptr};')
$lines.Add('    const auto it=std::lower_bound(std::begin(texts),std::end(texts),wanted,')
$lines.Add('        [language](const Text& text,const Text& value) {')
$lines.Add('            const int order=std::string_view(text.language).compare(language);')
$lines.Add('            return order<0 || (order==0 && std::strcmp(text.key,value.key)<0);')
$lines.Add('        });')
$lines.Add('    return it!=std::end(texts) && language==it->language && std::strcmp(key,it->key)==0 ? it->value : nullptr;')
$lines.Add('}')
$lines.Add('const char* translate(std::string_view language,const char* key) {')
$lines.Add('    if(!key)return "?";')
$lines.Add('    if(const auto* value=find_text(language,key))return value;')
$lines.Add('    if(language!="en")if(const auto* value=find_text("en",key))return value;')
$lines.Add('    return "?";')
$lines.Add('}')
$lines.Add('}')
$lines | Set-Content -Encoding utf8 (Join-Path $projectRoot 'src/localization.cpp')
