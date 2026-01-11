#include "agi_api.h"
#include "module_integration.h"
#include <sstream>
#include <iomanip>
#include <cmath>
#include <chrono>

extern std::string generateResponse(const std::string& input);
extern void sv(const std::string& filename);
extern void ld(const std::string& filename);

AGI_API::AGI_API(int port) : server_(std::make_unique<WebServer>(port)) {
    server_->register_route("POST", "/api/chat", [this](const HttpRequest& req) { return handle_chat(req); });
    server_->register_route("POST", "/api/save", [this](const HttpRequest& req) { return handle_save(req); });
    server_->register_route("POST", "/api/load", [this](const HttpRequest& req) { return handle_load(req); });
    server_->register_route("GET", "/", [this](const HttpRequest& req) { return handle_ui(req); });
}

AGI_API::~AGI_API() {
    stop();
}

void AGI_API::start() {
    server_->start();
}

void AGI_API::stop() {
    server_->stop();
}

std::string AGI_API::json_escape(const std::string& str) {
    std::ostringstream oss;
    for (char c : str) {
        switch (c) {
            case '"': oss << "\\\""; break;
            case '\\': oss << "\\\\"; break;
            case '\b': oss << "\\b"; break;
            case '\f': oss << "\\f"; break;
            case '\n': oss << "\\n"; break;
            case '\r': oss << "\\r"; break;
            case '\t': oss << "\\t"; break;
            default:
                if (c < 32) {
                    oss << "\\u" << std::hex << std::setfill('0') << std::setw(4) << (int)c;
                } else {
                    oss << c;
                }
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
        oss << "{\"status\": \"ok\", \"response\": \"" << json_escape(response) 
            << "\", \"valence\": " << std::fixed << std::setprecision(3) << S.current_valence
            << ", \"generation\": " << S.g << "}";
        resp.body = oss.str();
    } catch (const std::exception& e) {
        std::ostringstream oss;
        oss << "{\"status\": \"error\", \"message\": \"" << json_escape(e.what()) << "\"}";
        resp.body = oss.str();
        resp.status_code = 500;
    }
    
    return resp;
}

HttpResponse AGI_API::handle_save(const HttpRequest&) {
    HttpResponse resp;
    resp.status_code = 200;
    
    try {
        sv("state.dat");
        resp.body = "{\"status\": \"saved\"}";
    } catch (const std::exception& e) {
        resp.status_code = 500;
        std::ostringstream oss;
        oss << "{\"status\": \"error\", \"message\": \"" << json_escape(e.what()) << "\"}";
        resp.body = oss.str();
    }
    
    return resp;
}

HttpResponse AGI_API::handle_load(const HttpRequest&) {
    HttpResponse resp;
    resp.status_code = 200;
    
    try {
        ld("state.dat");
        resp.body = "{\"status\": \"loaded\"}";
    } catch (const std::exception& e) {
        resp.status_code = 500;
        std::ostringstream oss;
        oss << "{\"status\": \"error\", \"message\": \"" << json_escape(e.what()) << "\"}";
        resp.body = oss.str();
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
    <title>Nexus - WolfTech</title>
    <style>
        * { margin: 0; padding: 0; box-sizing: border-box; }
        
        body {
            font-family: -apple-system, BlinkMacSystemFont, "Segoe UI", Roboto, sans-serif;
            background: #fff;
            color: #000;
            height: 100vh;
            display: flex;
            flex-direction: column;
        }
        
        header {
            background: #fff;
            border-bottom: 1px solid #e0e0e0;
            padding: 12px 20px;
            display: flex;
            justify-content: space-between;
            align-items: center;
        }
        
        .logo {
            width: 32px;
            height: 32px;
            background: #000;
            border-radius: 6px;
            display: flex;
            align-items: center;
            justify-content: center;
            font-weight: 700;
            color: #fff;
            font-size: 16px;
        }
        
        h1 {
            font-size: 18px;
            font-weight: 600;
        }
        
        .header-left {
            display: flex;
            align-items: center;
            gap: 12px;
        }
        
        .btn {
            padding: 6px 14px;
            background: #fff;
            border: 1px solid #e0e0e0;
            border-radius: 6px;
            font-size: 13px;
            cursor: pointer;
            transition: all 0.2s;
        }
        
        .btn:hover {
            background: #f5f5f5;
        }
        
        .messages {
            flex: 1;
            overflow-y: auto;
            padding: 20px;
            max-width: 800px;
            width: 100%;
            margin: 0 auto;
        }
        
        .message {
            display: flex;
            gap: 10px;
            margin-bottom: 20px;
        }
        
        .avatar {
            width: 28px;
            height: 28px;
            border-radius: 6px;
            display: flex;
            align-items: center;
            justify-content: center;
            font-size: 13px;
            font-weight: 600;
            flex-shrink: 0;
            border: 1px solid #e0e0e0;
        }
        
        .message.user .avatar {
            background: #f5f5f5;
        }
        
        .message.ai .avatar {
            background: #000;
            color: #fff;
            border-color: #000;
        }
        
        .message-text {
            font-size: 14px;
            line-height: 1.6;
            padding: 10px 14px;
            border-radius: 8px;
            background: #fafafa;
            border: 1px solid #e0e0e0;
        }
        
        .message.user .message-text {
            background: #f5f5f5;
        }
        
        .input-area {
            padding: 16px 20px;
            background: #fff;
            border-top: 1px solid #e0e0e0;
        }
        
        .input-wrapper {
            max-width: 800px;
            margin: 0 auto;
            display: flex;
            gap: 10px;
        }
        
        textarea {
            flex: 1;
            padding: 10px 12px;
            border: 1px solid #e0e0e0;
            border-radius: 8px;
            font-size: 14px;
            font-family: inherit;
            resize: none;
            background: #fafafa;
        }
        
        textarea:focus {
            outline: none;
            border-color: #000;
            background: #fff;
        }
        
        .send-btn {
            padding: 10px 20px;
            background: #000;
            color: #fff;
            border: none;
            border-radius: 8px;
            font-size: 14px;
            font-weight: 600;
            cursor: pointer;
        }
        
        .send-btn:hover {
            background: #1a1a1a;
        }
        
        .send-btn:disabled {
            background: #e0e0e0;
            color: #999;
            cursor: not-allowed;
        }
        
        .typing {
            display: none;
            align-items: center;
            gap: 6px;
            padding: 10px 12px;
            color: #666;
            font-size: 13px;
            margin-bottom: 10px;
            background: #f5f5f5;
            border-radius: 8px;
            width: fit-content;
        }
        
        .typing.active { display: flex; }
        
        .dot {
            width: 4px;
            height: 4px;
            border-radius: 50%;
            background: #000;
            animation: typing 1.4s ease-in-out infinite;
        }
        
        .dot:nth-child(1) { animation-delay: 0s; }
        .dot:nth-child(2) { animation-delay: 0.2s; }
        .dot:nth-child(3) { animation-delay: 0.4s; }
        
        @keyframes typing {
            0%, 60%, 100% { opacity: 0.3; }
            30% { opacity: 1; }
        }
        
        .empty {
            height: 100%;
            display: flex;
            flex-direction: column;
            align-items: center;
            justify-content: center;
            gap: 16px;
        }
        
        .empty-icon {
            width: 60px;
            height: 60px;
            background: #000;
            border-radius: 12px;
            display: flex;
            align-items: center;
            justify-content: center;
            font-size: 28px;
            font-weight: 700;
            color: #fff;
        }
        
        .empty-text {
            font-size: 14px;
            color: #666;
            max-width: 300px;
            text-align: center;
        }
        
        ::-webkit-scrollbar { width: 8px; }
        ::-webkit-scrollbar-thumb { background: #e0e0e0; border-radius: 4px; }
        ::-webkit-scrollbar-thumb:hover { background: #d0d0d0; }
        
        @media (max-width: 768px) {
            .messages { padding: 16px; }
            .input-area { padding: 12px 16px; }
        }
    </style>
</head>
<body>
    <header>
        <div class="header-left">
            <div class="logo">N</div>
            <h1>Nexus</h1>
        </div>
        <button class="btn" onclick="clearChat()">Clear</button>
    </header>
    
    <div class="messages" id="messages">
        <div class="empty">
            <div class="empty-icon">N</div>
            <div class="empty-text">Nexus</div>
        </div>
    </div>
    
    <div class="input-area">
        <div class="typing" id="typing">
            <span>Processing</span>
            <div class="dot"></div>
            <div class="dot"></div>
            <div class="dot"></div>
        </div>
        <div class="input-wrapper">
            <textarea id="input" placeholder="Message Nexus..." rows="1"></textarea>
            <button class="send-btn" id="send">Send</button>
        </div>
    </div>
    
    <footer style="padding: 8px 20px; text-align: center; font-size: 12px; color: #999; border-top: 1px solid #e0e0e0; background: #fff;">
        WolfTech Innovations
    </footer>
    
    <script type="module">
        import { pipeline, env } from 'https://cdn.jsdelivr.net/npm/@xenova/transformers@2.17.2';
        
        env.allowLocalModels = false;
        env.backends.onnx.wasm.numThreads = 1;
        
        let model = null;
        let loading = false;
        let history = [];
        let sending = false;
        let first = true;
        
        const input = document.getElementById('input');
        const sendBtn = document.getElementById('send');
        const messages = document.getElementById('messages');
        const typing = document.getElementById('typing');
        
        async function initModel() {
            if (model || loading) return;
            loading = true;
            try {
                console.log('Loading tiny model...');
                // Use the smallest possible model - Flan-T5 small (77MB)
                model = await pipeline('text2text-generation', 'Xenova/flan-t5-small', {
                    quantized: true,
                    progress_callback: (progress) => {
                        if (progress.status === 'progress') {
                            console.log(`Loading: ${progress.file} - ${Math.round(progress.progress)}%`);
                        }
                    }
                });
                console.log('Model loaded successfully');
            } catch (e) {
                console.error('Model load failed:', e);
                model = null;
            }
            loading = false;
        }
        
        // Load model gradually after page loads
        setTimeout(() => {
            initModel();
        }, 2000);
        
        async function enhance(text) {
            if (!model) {
                console.warn('Model not ready, returning original text');
                return text;
            }
            try {
                // Very simple prompt for the tiny model
                const prompt = `Fix grammar and make readable: ${text}`;
                
                const result = await model(prompt, { 
                    max_new_tokens: 256,
                    temperature: 0.3,
                    do_sample: false
                });
                
                const enhanced = result[0].generated_text.trim();
                
                if (!enhanced || enhanced.length < 5) {
                    return text;
                }
                
                return enhanced;
            } catch (e) {
                console.error('Enhancement failed:', e);
                return text;
            }
        }
        
        input.addEventListener('input', function() {
            this.style.height = 'auto';
            this.style.height = Math.min(this.scrollHeight, 120) + 'px';
        });
        
        async function send() {
            if (sending) return;
            
            const msg = input.value.trim();
            if (!msg) return;
            
            sending = true;
            
            if (first) {
                messages.innerHTML = '';
                first = false;
            }
            
            add('user', msg);
            history.push({ role: 'user', text: msg, time: Date.now() });
            
            input.value = '';
            input.style.height = 'auto';
            
            typing.classList.add('active');
            sendBtn.disabled = true;
            
            try {
                const res = await fetch('/api/chat', {
                    method: 'POST',
                    headers: { 'Content-Type': 'application/json' },
                    body: JSON.stringify({ message: msg })
                });
                
                const data = await res.json();
                
                if (data.status === 'ok') {
                    let response = data.response;
                    
                    // Only enhance if model is ready, otherwise show original
                    if (model) {
                        typing.querySelector('span').textContent = 'Enhancing coherence';
                        try {
                            response = await enhance(response);
                        } catch (e) {
                            console.error('Enhancement error:', e);
                            // Fall back to original on error
                        }
                    }
                    
                    typing.classList.remove('active');
                    typing.querySelector('span').textContent = 'Processing';
                    
                    add('ai', response);
                    history.push({ role: 'ai', text: response, time: Date.now() });
                    save();
                } else {
                    typing.classList.remove('active');
                    add('ai', 'Error: ' + data.message);
                }
            } catch (e) {
                typing.classList.remove('active');
                add('ai', 'Connection error');
            }
            
            sending = false;
            sendBtn.disabled = false;
            input.focus();
        }
        
        function add(role, text) {
            const msg = document.createElement('div');
            msg.className = 'message ' + role;
            
            const avatar = document.createElement('div');
            avatar.className = 'avatar';
            avatar.textContent = role === 'user' ? 'U' : 'N';
            
            const content = document.createElement('div');
            content.className = 'message-text';
            content.textContent = text;
            
            msg.appendChild(avatar);
            msg.appendChild(content);
            messages.appendChild(msg);
            
            messages.scrollTop = messages.scrollHeight;
        }
        
        function save() {
            try {
                localStorage.setItem('nexus_history', JSON.stringify(history));
            } catch (e) {}
        }
        
        function load() {
            try {
                const saved = localStorage.getItem('nexus_history');
                if (saved) {
                    history = JSON.parse(saved);
                    if (history.length > 0) {
                        first = false;
                        messages.innerHTML = '';
                        history.forEach(m => add(m.role, m.text));
                    }
                }
            } catch (e) {}
        }
        
        window.clearChat = function() {
            if (confirm('Clear all messages?')) {
                history = [];
                first = true;
                localStorage.removeItem('nexus_history');
                messages.innerHTML = '<div class="empty"><div class="empty-icon">N</div><div class="empty-text">Nexus</div></div>';
            }
        };
        
        sendBtn.addEventListener('click', send);
        input.addEventListener('keydown', e => {
            if (e.key === 'Enter' && !e.shiftKey) {
                e.preventDefault();
                send();
            }
        });
        
        load();
        input.focus();
        
        window.addEventListener('beforeunload', () => {
            save();
            navigator.sendBeacon('/api/save');
        });
    </script>
</body>
</html>)html";
    
    return resp;
}
