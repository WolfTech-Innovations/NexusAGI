#include "agi_api.h"
#include "module_integration.h"
#include <onnxruntime_cxx_api.h>
#include <sstream>
#include <iomanip>
#include <fstream>
#include <cstdlib>
#include <vector>
#include <regex>

extern std::string generateResponse(const std::string& input);
extern void sv(const std::string& filename);
extern void ld(const std::string& filename);

// Simple tokenizer for Gemma
class SimpleTokenizer {
    std::map<std::string, int> vocab;
    std::map<int, std::string> reverse_vocab;
public:
    SimpleTokenizer() {
        // Basic vocabulary - in production, load from tokenizer.json
        vocab["<pad>"] = 0;
        vocab["<eos>"] = 1;
        vocab["<bos>"] = 2;
        for(int i = 0; i < 256; i++) {
            std::string s(1, (char)i);
            vocab[s] = i + 3;
            reverse_vocab[i + 3] = s;
        }
    }
    
    std::vector<int64_t> encode(const std::string& text) {
        std::vector<int64_t> tokens;
        tokens.push_back(2); // <bos>
        for(char c : text) {
            std::string s(1, c);
            tokens.push_back(vocab[s]);
        }
        return tokens;
    }
    
    std::string decode(const std::vector<int64_t>& tokens) {
        std::string result;
        for(auto t : tokens) {
            if(t >= 3 && t < 259) result += reverse_vocab[t];
        }
        return result;
    }
};

class CoherenceModel {
    Ort::Env env;
    Ort::SessionOptions session_options;
    std::unique_ptr<Ort::Session> session;
    SimpleTokenizer tokenizer;
    bool loaded;
    
public:
    CoherenceModel() : env(ORT_LOGGING_LEVEL_ERROR, "nexus"), loaded(false) {
        session_options.SetIntraOpNumThreads(1);
        session_options.SetGraphOptimizationLevel(GraphOptimizationLevel::ORT_ENABLE_ALL);
    }
    
    bool download_model() {
        std::string model_dir = "./gemma_model/";
        std::string model_file = model_dir + "model_q4.onnx";
        
        // Check if model exists
        std::ifstream f(model_file);
        if(f.good()) return true;
        
        // Create directory
        system("mkdir -p ./gemma_model");
        
        // Download model from Hugging Face
        std::string url = "https://huggingface.co/onnx-community/gemma-3-1b-it-ONNX/resolve/main/onnx/model_q4.onnx";
        std::string cmd = "curl -L " + url + " -o " + model_file + " 2>/dev/null";
        
        std::cout << "Downloading Gemma 3 1B model..." << std::endl;
        int ret = system(cmd.c_str());
        
        if(ret != 0) {
            std::cerr << "Failed to download model" << std::endl;
            return false;
        }
        
        std::cout << "Model downloaded successfully" << std::endl;
        return true;
    }
    
    bool load() {
        if(loaded) return true;
        
        if(!download_model()) return false;
        
        try {
            std::string model_path = "./gemma_model/model_q4.onnx";
            session = std::make_unique<Ort::Session>(env, model_path.c_str(), session_options);
            loaded = true;
            std::cout << "Gemma model loaded successfully" << std::endl;
            return true;
        } catch(const Ort::Exception& e) {
            std::cerr << "ONNX Error: " << e.what() << std::endl;
            return false;
        }
    }
    
    std::string enhance(const std::string& raw_text) {
        if(!loaded && !load()) return clean_text(raw_text);
        
        try {
            // Clean markers first
            std::string cleaned = clean_text(raw_text);
            
            // Prepare input
            std::string prompt = "Fix grammar and make this natural: " + cleaned;
            auto input_tokens = tokenizer.encode(prompt);
            
            // Create input tensor
            std::vector<int64_t> input_shape = {1, (int64_t)input_tokens.size()};
            auto memory_info = Ort::MemoryInfo::CreateCpu(OrtArenaAllocator, OrtMemTypeDefault);
            Ort::Value input_tensor = Ort::Value::CreateTensor<int64_t>(
                memory_info, input_tokens.data(), input_tokens.size(), 
                input_shape.data(), input_shape.size()
            );
            
            // Run inference
            const char* input_names[] = {"input_ids"};
            const char* output_names[] = {"logits"};
            auto output_tensors = session->Run(Ort::RunOptions{nullptr}, 
                input_names, &input_tensor, 1, output_names, 1);
            
            // Get output (simplified - just return cleaned text for now)
            // In production, implement proper decoding
            
            // Capitalize and punctuate
            if(cleaned.length() > 0) {
                cleaned[0] = std::toupper(cleaned[0]);
                if(cleaned.back() != '.' && cleaned.back() != '!' && cleaned.back() != '?') {
                    cleaned += '.';
                }
            }
            
            return cleaned;
        } catch(const Ort::Exception& e) {
            std::cerr << "Inference error: " << e.what() << std::endl;
            return clean_text(raw_text);
        }
    }
    
private:
    std::string clean_text(const std::string& text) {
        std::string result = text;
        result = std::regex_replace(result, std::regex("\\[NEXUS\\]:\\s*"), "");
        result = std::regex_replace(result, std::regex("\\[positive\\]|\\[negative\\]|\\[neutral\\]"), "");
        result = std::regex_replace(result, std::regex("^\\s+|\\s+$"), "");
        return result;
    }
};

static CoherenceModel coherence_model;

AGI_API::AGI_API(int port) : server_(std::make_unique<WebServer>(port)) {
    server_->register_route("POST", "/api/chat", [this](const HttpRequest& req) { return handle_chat(req); });
    server_->register_route("POST", "/api/save", [this](const HttpRequest& req) { return handle_save(req); });
    server_->register_route("POST", "/api/load", [this](const HttpRequest& req) { return handle_load(req); });
    server_->register_route("GET", "/", [this](const HttpRequest& req) { return handle_ui(req); });
    
    // Load model in background
    std::thread([](){ coherence_model.load(); }).detach();
}

AGI_API::~AGI_API() { stop(); }
void AGI_API::start() { server_->start(); }
void AGI_API::stop() { server_->stop(); }

std::string AGI_API::json_escape(const std::string& str) {
    std::ostringstream oss;
    for (char c : str) {
        switch (c) {
            case '"': oss << "\\\""; break;
            case '\\': oss << "\\\\"; break;
            case '\n': oss << "\\n"; break;
            case '\r': oss << "\\r"; break;
            case '\t': oss << "\\t"; break;
            default: oss << c;
        }
    }
    return oss.str();
}

HttpResponse AGI_API::handle_chat(const HttpRequest& req) {
    HttpResponse resp;
    resp.status_code = 200;
    
    std::string message;
    size_t msg_pos = req.body.find("\"message\":");
    if (msg_pos != std::string::npos) {
        size_t start = req.body.find('"', msg_pos + 10) + 1;
        size_t end = req.body.find('"', start);
        message = req.body.substr(start, end - start);
    }
    
    try {
        std::string response = generateResponse(message);
        std::string enhanced = coherence_model.enhance(response);
        
        std::ostringstream oss;
        oss << "{\"status\":\"ok\",\"response\":\"" << json_escape(enhanced) << "\"}";
        resp.body = oss.str();
    } catch (const std::exception& e) {
        resp.body = "{\"status\":\"error\",\"message\":\"" + json_escape(e.what()) + "\"}";
        resp.status_code = 500;
    }
    return resp;
}

HttpResponse AGI_API::handle_save(const HttpRequest&) {
    HttpResponse resp;
    resp.status_code = 200;
    try {
        sv("state.dat");
        resp.body = "{\"status\":\"saved\"}";
    } catch (const std::exception& e) {
        resp.status_code = 500;
        resp.body = "{\"status\":\"error\",\"message\":\"" + json_escape(e.what()) + "\"}";
    }
    return resp;
}

HttpResponse AGI_API::handle_load(const HttpRequest&) {
    HttpResponse resp;
    resp.status_code = 200;
    try {
        ld("state.dat");
        resp.body = "{\"status\":\"loaded\"}";
    } catch (const std::exception& e) {
        resp.status_code = 500;
        resp.body = "{\"status\":\"error\",\"message\":\"" + json_escape(e.what()) + "\"}";
    }
    return resp;
}

HttpResponse AGI_API::handle_ui(const HttpRequest&) {
    HttpResponse resp;
    resp.status_code = 200;
    resp.headers["Content-Type"] = "text/html; charset=utf-8";
    resp.body = R"html(<!DOCTYPE html>
<html lang="en">
<head>
<meta charset="UTF-8">
<meta name="viewport" content="width=device-width, initial-scale=1.0">
<title>Nexus</title>
<style>
*{margin:0;padding:0;box-sizing:border-box}
body{font-family:-apple-system,BlinkMacSystemFont,"Segoe UI",Roboto,sans-serif;background:#fff;color:#000;height:100vh;display:flex;flex-direction:column}
header{background:#fff;border-bottom:1px solid #e0e0e0;padding:12px 20px;display:flex;justify-content:space-between;align-items:center}
.logo{width:32px;height:32px;background:#000;border-radius:6px;display:flex;align-items:center;justify-content:center;font-weight:700;color:#fff;font-size:16px}
h1{font-size:18px;font-weight:600}
.left{display:flex;align-items:center;gap:12px}
.btn{padding:6px 14px;background:#fff;border:1px solid #e0e0e0;border-radius:6px;font-size:13px;cursor:pointer;transition:all .2s}
.btn:hover{background:#f5f5f5}
.messages{flex:1;overflow-y:auto;padding:20px;max-width:800px;width:100%;margin:0 auto}
.message{display:flex;gap:10px;margin-bottom:20px}
.avatar{width:28px;height:28px;border-radius:6px;display:flex;align-items:center;justify-content:center;font-size:13px;font-weight:600;flex-shrink:0;border:1px solid #e0e0e0}
.message.user .avatar{background:#f5f5f5}
.message.ai .avatar{background:#000;color:#fff;border-color:#000}
.text{font-size:14px;line-height:1.6;padding:10px 14px;border-radius:8px;background:#fafafa;border:1px solid #e0e0e0}
.message.user .text{background:#f5f5f5}
.input-area{padding:16px 20px;background:#fff;border-top:1px solid #e0e0e0}
.wrapper{max-width:800px;margin:0 auto;display:flex;gap:10px}
textarea{flex:1;padding:10px 12px;border:1px solid #e0e0e0;border-radius:8px;font-size:14px;font-family:inherit;resize:none;background:#fafafa}
textarea:focus{outline:none;border-color:#000;background:#fff}
.send{padding:10px 20px;background:#000;color:#fff;border:none;border-radius:8px;font-size:14px;font-weight:600;cursor:pointer}
.send:hover{background:#1a1a1a}
.send:disabled{background:#e0e0e0;color:#999;cursor:not-allowed}
.typing{display:none;align-items:center;gap:6px;padding:10px 12px;color:#666;font-size:13px;margin-bottom:10px;background:#f5f5f5;border-radius:8px;width:fit-content}
.typing.active{display:flex}
.dot{width:4px;height:4px;border-radius:50%;background:#000;animation:t 1.4s ease-in-out infinite}
.dot:nth-child(1){animation-delay:0s}
.dot:nth-child(2){animation-delay:.2s}
.dot:nth-child(3){animation-delay:.4s}
@keyframes t{0%,60%,100%{opacity:.3}30%{opacity:1}}
.empty{height:100%;display:flex;flex-direction:column;align-items:center;justify-content:center;gap:16px}
.empty-icon{width:60px;height:60px;background:#000;border-radius:12px;display:flex;align-items:center;justify-content:center;font-size:28px;font-weight:700;color:#fff}
footer{padding:8px;text-align:center;font-size:12px;color:#999;border-top:1px solid #e0e0e0}
::-webkit-scrollbar{width:8px}
::-webkit-scrollbar-thumb{background:#e0e0e0;border-radius:4px}
</style>
</head>
<body>
<header>
<div class="left"><div class="logo">N</div><h1>Nexus</h1></div>
<button class="btn" onclick="clearChat()">Clear</button>
</header>
<div class="messages" id="msg"><div class="empty"><div class="empty-icon">N</div><div class="empty-text">Nexus</div></div></div>
<div class="input-area">
<div class="typing" id="typ"><span>Processing</span><div class="dot"></div><div class="dot"></div><div class="dot"></div></div>
<div class="wrapper"><textarea id="inp" placeholder="Message Nexus..." rows="1"></textarea><button class="send" id="btn">Send</button></div>
</div>
<footer>WolfTech Innovations</footer>
<script>
let h=[],s,f=1;
const i=document.getElementById('inp'),b=document.getElementById('btn'),g=document.getElementById('msg'),t=document.getElementById('typ');
i.addEventListener('input',function(){this.style.height='auto';this.style.height=Math.min(this.scrollHeight,120)+'px'});
async function send(){if(s)return;const v=i.value.trim();if(!v)return;s=1;if(f){g.innerHTML='';f=0}add('user',v);h.push({role:'user',text:v,time:Date.now()});i.value='';i.style.height='auto';t.classList.add('active');b.disabled=true;try{const r=await fetch('/api/chat',{method:'POST',headers:{'Content-Type':'application/json'},body:JSON.stringify({message:v})});const d=await r.json();t.classList.remove('active');if(d.status==='ok'){add('ai',d.response);h.push({role:'ai',text:d.response,time:Date.now()});save()}else{add('ai','Error: '+d.message)}}catch(e){t.classList.remove('active');add('ai','Connection error')}s=0;b.disabled=false;i.focus()}
function add(r,x){const d=document.createElement('div');d.className='message '+r;const a=document.createElement('div');a.className='avatar';a.textContent=r==='user'?'U':'N';const c=document.createElement('div');c.className='text';c.textContent=x;d.appendChild(a);d.appendChild(c);g.appendChild(d);g.scrollTop=g.scrollHeight}
function save(){try{localStorage.setItem('nexus_history',JSON.stringify(h))}catch(e){}}
function load(){try{const d=localStorage.getItem('nexus_history');if(d){h=JSON.parse(d);if(h.length>0){f=0;g.innerHTML='';h.forEach(m=>add(m.role,m.text))}}}catch(e){}}
window.clearChat=function(){if(confirm('Clear all messages?')){h=[];f=1;localStorage.removeItem('nexus_history');g.innerHTML='<div class="empty"><div class="empty-icon">N</div><div class="empty-text">Nexus</div></div>'}};
b.addEventListener('click',send);
i.addEventListener('keydown',e=>{if(e.key==='Enter'&&!e.shiftKey){e.preventDefault();send()}});
load();i.focus();
window.addEventListener('beforeunload',()=>{save();navigator.sendBeacon('/api/save')});
</script>
</body>
</html>)html";
    return resp;
}