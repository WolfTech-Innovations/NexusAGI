#include "agi_api.h"
#include "module_integration.h"
#include <sstream>
#include <iomanip>

extern std::string generateResponse(const std::string& input);
extern void sv(const std::string& filename);
extern void ld(const std::string& filename);

AGI_API::AGI_API(int port) : server_(std::make_unique<WebServer>(port)) {
    server_->register_route("POST", "/api/chat", [this](const HttpRequest& req) { return handle_chat(req); });
    server_->register_route("POST", "/api/save", [this](const HttpRequest& req) { return handle_save(req); });
    server_->register_route("POST", "/api/load", [this](const HttpRequest& req) { return handle_load(req); });
    server_->register_route("GET", "/", [this](const HttpRequest& req) { return handle_ui(req); });
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
        std::ostringstream oss;
        oss << "{\"status\":\"ok\",\"response\":\"" << json_escape(response) << "\"}";
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
<script type="module">
import{pipeline,env}from'https://cdn.jsdelivr.net/npm/@xenova/transformers@2.17.2';
env.allowLocalModels=false;env.backends.onnx.wasm.numThreads=1;
let m,l,h=[],s,f=1;
const i=document.getElementById('inp'),b=document.getElementById('btn'),g=document.getElementById('msg'),t=document.getElementById('typ');
async function init(){if(m||l)return;l=1;try{m=await pipeline('text-generation','onnx-community/Qwen2.5-0.5B-Instruct',{dtype:'q4',device:'wasm'})}catch(e){m=null}l=0}
setTimeout(init,2000);
i.addEventListener('input',function(){this.style.height='auto';this.style.height=Math.min(this.scrollHeight,120)+'px'});
async function enh(x){if(!m)return x;try{let c=x.replace(/\[NEXUS\]:\s*/gi,'').replace(/\[positive\]|\[negative\]|\[neutral\]/gi,'').trim();const o=await m([{role:'system',content:'You are a professional text editor. Take garbled, unclear, or poorly written text and transform it into clear, standard, natural English. Fix all grammar errors, improve sentence structure, and make it sound like a normal human conversation. Do not add extra information - just make the existing meaning clear and readable.'},{role:'user',content:`Transform this into clear, natural, standard English: ${c}`}],{max_new_tokens:300,temperature:.5,top_p:.95,do_sample:true,repetition_penalty:1.1});let e=o[0].generated_text.at(-1).content.trim().replace(/^["'\s]+|["'\s]+$/g,'').replace(/^(Here's|Here is|The text says|This means).*?:\s*/i,'').replace(/\n+/g,' ').replace(/\s+/g,' ').trim();if(e.length>0)e=e.charAt(0).toUpperCase()+e.slice(1);if(e.length>0&&!e.match(/[.!?]$/))e+='.';return e.length>4?e:c}catch(e){return x.replace(/\[NEXUS\]:\s*/gi,'').replace(/\[positive\]|\[negative\]|\[neutral\]/gi,'').trim()}}
async function send(){if(s)return;const v=i.value.trim();if(!v)return;s=1;if(f){g.innerHTML='';f=0}add('user',v);h.push({role:'user',text:v,time:Date.now()});i.value='';i.style.height='auto';t.classList.add('active');b.disabled=true;try{const r=await fetch('/api/chat',{method:'POST',headers:{'Content-Type':'application/json'},body:JSON.stringify({message:v})});const d=await r.json();let p=d.response;if(m){t.querySelector('span').textContent='Enhancing';try{p=await enh(p)}catch(e){}}t.classList.remove('active');t.querySelector('span').textContent='Processing';add('ai',p);h.push({role:'ai',text:p,time:Date.now()});save()}catch(e){t.classList.remove('active');add('ai','Connection error')}s=0;b.disabled=false;i.focus()}
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
