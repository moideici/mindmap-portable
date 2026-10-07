"""Check UTF-8 wrapping against v2 and measure long-note prefix processing.

Run: python3 sources/tests/wrap_performance.py [g++]
Uses the original Git commit as an independent compatibility reference.
"""
from pathlib import Path
import subprocess
import sys
import tempfile

ROOT = Path(__file__).resolve().parents[1]
BASE = "98b56d2732d25a2def9cfb2df65ef1ac3f735ea8"
original = subprocess.check_output(
    ["git", "show", f"{BASE}:sources/src/geometry.hpp"], cwd=ROOT, text=True
)
reference = original.split("inline std::vector<std::string> wrap(", 1)[1]
reference = "inline std::vector<std::string> reference_wrap(" + reference.split(
    "// Text metrics", 1
)[0]
driver = r'''
#include <chrono>
#include <random>
#include <iostream>

int main() {
    std::mt19937 rng(42);
    const std::vector<std::string> tokens={"a"," ","\n","\r","é","界","🗺","\t","…"};
    size_t checked=0;
    auto check=[&](const std::string& s,size_t width,size_t lines){
        // The original dereferences an empty vector for multiline text with zero lines.
        if(lines==0){
            if(!document::wrap(s,width,lines).empty())std::exit(1);
            ++checked;return;
        }
        if(document::wrap(s,width,lines)!=document::reference_wrap(s,width,lines)) {
            std::cerr<<"Wrapping mismatch: case "<<checked<<", width "<<width<<", lines "<<lines<<"\n";
            std::exit(1);
        }
        ++checked;
    };
    for(const std::string s:{"", "\n", "\r\n", "abcd", "abcd\n", "     ", "ab cd ef gh ij", "é界🗺 é界🗺 é界🗺"})
        for(size_t width=0;width<10;++width)for(size_t lines=0;lines<8;++lines)check(s,width,lines);
    for(int i=0;i<30000;++i){
        std::string s;const int length=rng()%400;
        for(int j=0;j<length;++j)s+=tokens[rng()%tokens.size()];
        check(s,rng()%70,rng()%9);
    }
    std::string note;
    while(note.size()<15900)note+="Une note de révision très longue avec des détails. ";
    for(size_t width:{4,27,40,20000})for(size_t lines:{0,1,4,1000})check(note,width,lines);
    std::cout<<"Compatibility: "<<checked<<" cases PASS\n";
    auto measure=[&](auto fn,const std::string& text){
        size_t checksum=0;const auto start=std::chrono::steady_clock::now();
        for(int i=0;i<10000;++i){auto result=fn(text,27,4);for(const auto& line:result)checksum+=line.size();}
        const auto ms=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-start).count();
        return std::pair{ms,checksum};
    };
    for(const auto& text:{std::string("Une courte note de révision."),note}){
        auto before=measure(document::reference_wrap,text),after=measure(document::wrap,text);
        if(before.second!=after.second)return 1;
        std::cout<<"Note "<<text.size()<<" bytes: before="<<before.first<<"ms after="<<after.first
                 <<"ms speedup="<<before.first/after.first<<"x\n";
    }
}
'''
with tempfile.TemporaryDirectory(prefix="mindmap-wrap-") as temporary:
    source = Path(temporary) / "wrap_check.cpp"
    executable = Path(temporary) / "wrap_check"
    source.write_text(
        '#include "geometry.hpp"\nnamespace document {\n'
        + reference + "\n}\n" + driver, encoding="utf-8"
    )
    subprocess.run(
        [sys.argv[1] if len(sys.argv) > 1 else "g++", "-std=c++20", "-O2",
         "-I", str(ROOT / "src"), str(source), "-o", str(executable)],
        check=True, timeout=120,
    )
    subprocess.run([str(executable)], check=True, timeout=60)
