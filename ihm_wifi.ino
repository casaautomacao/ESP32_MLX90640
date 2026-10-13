#include <Wire.h>
#include <WiFi.h>
#include <WebServer.h>
#include <Adafruit_MLX90640.h>
#include <math.h>

// Preencha aqui, no seu computador:
const char* WIFI_SSID  = "crescer";
const char* WIFI_SENHA = "cpb32832";

Adafruit_MLX90640 mlx;
WebServer server(80);

float frame[32 * 24];

const char PAGINA[] PROGMEM = R"HTML(
<!DOCTYPE html>
<html lang="pt-BR">
<head>
<meta charset="utf-8">
<meta name="viewport" content="width=device-width, initial-scale=1">
<title>Camera termica</title>
<style>
  * { box-sizing: border-box; }
  body {
    margin: 0; padding: 20px;
    background: #111827; color: #e5e7eb;
    font-family: Arial, sans-serif;
  }
  main { max-width: 850px; margin: auto; }
  h1 { font-size: 24px; }
  .painel {
    background: #1f2937; padding: 16px;
    border-radius: 12px; margin-bottom: 16px;
  }
  .dados {
    display: flex; flex-wrap: wrap;
    gap: 20px; margin-bottom: 14px;
  }
  .dados strong { display: block; font-size: 24px; }
  canvas {
    display: block; width: 100%;
    aspect-ratio: 4 / 3; background: #000;
    touch-action: manipulation;
  }
  .barra {
    height: 18px; margin-top: 12px;
    background: linear-gradient(to right,
      #000028, #0000ff, #00ffff, #ffff00, #ff0000);
  }
  .limites { display: flex; justify-content: space-between; }
  .controles {
    display: flex; flex-wrap: wrap;
    gap: 12px; align-items: center;
  }
  button, input {
    padding: 8px; border-radius: 6px;
    border: 1px solid #64748b;
  }
  input[type=number] { width: 80px; }
  #status { color: #93c5fd; }
</style>
</head>
<body>
<main>
  <h1>Camera termica — MLX90640</h1>

  <div class="painel">
    <div class="dados">
      <div>Minima<strong id="min">--</strong></div>
      <div>Maxima<strong id="max">--</strong></div>
      <div>Sensor<strong id="sensor">--</strong></div>
    </div>

    <canvas id="imagem" width="640" height="480"></canvas>
    <div class="barra"></div>
    <div class="limites">
      <span id="baixo">--</span>
      <span id="alto">--</span>
    </div>
    <p id="ponto">Clique na imagem para ler um pixel.</p>
  </div>

  <div class="painel controles">
    <label>
      <input id="auto" type="checkbox" checked>
      Escala automatica
    </label>
    <label>De <input id="de" type="number" value="20"> °C</label>
    <label>Ate <input id="ate" type="number" value="40"> °C</label>
    <label>
      <input id="suave" type="checkbox">
      Suavizar
    </label>
    <button id="pausa">Pausar</button>
  </div>

  <p id="status">Aguardando primeira leitura...</p>
  <small>
    Resolucao real: 32 × 24 pixels.
    Suavizacao altera apenas a aparencia.
  </small>
</main>

<script>
const el = id => document.getElementById(id);
const tela = el('imagem');
const ctx = tela.getContext('2d');

const pequena = document.createElement('canvas');
pequena.width = 32;
pequena.height = 24;
const pctx = pequena.getContext('2d');

let dados = null;
let pausado = false;
let selecionado = null;

// Paleta: azul escuro, azul, ciano, amarelo, vermelho.
const cores = [
  [0, 0, 40],
  [0, 0, 255],
  [0, 255, 255],
  [255, 255, 0],
  [255, 0, 0]
];

function cor(t) {
  t = Math.max(0, Math.min(1, t));
  const pos = t * (cores.length - 1);
  const i = Math.min(cores.length - 2, Math.floor(pos));
  const f = pos - i;
  return cores[i].map((v, c) =>
    Math.round(v + (cores[i + 1][c] - v) * f)
  );
}

function desenhar() {
  if (!dados) return;

  const lo = el('auto').checked ? dados.min : Number(el('de').value);
  const hi = el('auto').checked ? dados.max : Number(el('ate').value);

  if (!Number.isFinite(lo) || !Number.isFinite(hi) || hi < lo) {
    el('status').textContent = 'Confira os limites da escala.';
    return;
  }

  const faixa = Math.max(hi - lo, 0.1);
  const img = pctx.createImageData(32, 24);

  dados.pixels.forEach((temp, i) => {
    const rgb = cor((temp - lo) / faixa);
    img.data[i * 4] = rgb[0];
    img.data[i * 4 + 1] = rgb[1];
    img.data[i * 4 + 2] = rgb[2];
    img.data[i * 4 + 3] = 255;
  });

  pctx.putImageData(img, 0, 0);
  ctx.imageSmoothingEnabled = el('suave').checked;
  ctx.drawImage(pequena, 0, 0, tela.width, tela.height);

  el('min').textContent = dados.min.toFixed(1) + ' °C';
  el('max').textContent = dados.max.toFixed(1) + ' °C';
  el('sensor').textContent = dados.sensor.toFixed(1) + ' °C';
  el('baixo').textContent = lo.toFixed(1) + ' °C';
  el('alto').textContent = hi.toFixed(1) + ' °C';

  if (selecionado !== null) {
    const x = selecionado % 32;
    const y = Math.floor(selecionado / 32);

    ctx.strokeStyle = '#ffffff';
    ctx.lineWidth = 2;
    ctx.strokeRect(x * 20 + 1, y * 20 + 1, 18, 18);

    el('ponto').textContent =
      `Coluna ${x}, linha ${y}: ` +
      dados.pixels[selecionado].toFixed(1) + ' °C';
  }
}

tela.addEventListener('click', event => {
  const r = tela.getBoundingClientRect();
  const x = Math.max(0, Math.min(31,
    Math.floor((event.clientX - r.left) / r.width * 32)));
  const y = Math.max(0, Math.min(23,
    Math.floor((event.clientY - r.top) / r.height * 24)));
  selecionado = y * 32 + x;
  desenhar();
});

['auto', 'de', 'ate', 'suave'].forEach(id => {
  el(id).addEventListener('change', desenhar);
});

el('pausa').onclick = () => {
  pausado = !pausado;
  el('pausa').textContent = pausado ? 'Continuar' : 'Pausar';
  el('status').textContent = pausado ? 'Imagem pausada.' : 'Retomando...';
};

async function atualizar() {
  if (!pausado) {
    const controle = new AbortController();
    const timeout = setTimeout(() => controle.abort(), 8000);

    try {
      const resposta = await fetch('/frame', {
        cache: 'no-store',
        signal: controle.signal
      });
      if (!resposta.ok) throw new Error(await resposta.text());

      const novo = await resposta.json();
      if (!pausado) {
        dados = novo;
        el('status').textContent =
          'Leitura recebida: ' + new Date().toLocaleTimeString();
        desenhar();
      }
    } catch (erro) {
      if (!pausado) {
        el('status').textContent = 'Falha na leitura: ' + erro.message;
      }
    } finally {
      clearTimeout(timeout);
    }
  }

  // Uma requisicao por vez.
  setTimeout(atualizar, 250);
}

atualizar();
</script>
</body>
</html>
)HTML";

void enviarFrame() {
  int erro = mlx.getFrame(frame);

  if (erro != 0) {
    Serial.printf("Erro do sensor: %d\n", erro);
    server.send(503, "text/plain", "Erro de leitura do MLX90640");
    return;
  }

  float temperaturaSensor = mlx.getTa(false);
  float minima = frame[0];
  float maxima = frame[0];

  if (!isfinite(temperaturaSensor)) {
    server.send(503, "text/plain", "Temperatura interna invalida");
    return;
  }

  for (int i = 0; i < 768; i++) {
    if (!isfinite(frame[i])) {
      server.send(503, "text/plain", "Pixel com leitura invalida");
      return;
    }
    if (frame[i] < minima) minima = frame[i];
    if (frame[i] > maxima) maxima = frame[i];
  }

  String json;
  json.reserve(10000);

  json = "{\"min\":";
  json += String(minima, 2);
  json += ",\"max\":";
  json += String(maxima, 2);
  json += ",\"sensor\":";
  json += String(temperaturaSensor, 2);
  json += ",\"pixels\":[";

  for (int i = 0; i < 768; i++) {
    if (i > 0) json += ',';
    json += String(frame[i], 2);
  }

  json += "]}";

  server.sendHeader("Cache-Control", "no-store");
  server.send(200, "application/json", json);
}

void setup() {
  Serial.begin(115200);
  delay(500);

  Wire.begin(21, 22);

  if (!mlx.begin(0x33, &Wire)) {
    Serial.println("MLX90640 nao encontrado.");
    while (true) delay(1000);
  }

  Wire.setClock(400000);
  mlx.setMode(MLX90640_CHESS);
  mlx.setResolution(MLX90640_ADC_18BIT);
  mlx.setRefreshRate(MLX90640_4_HZ);

  Serial.println("Sensor iniciado. Conectando ao Wi-Fi...");

  WiFi.mode(WIFI_STA);
  WiFi.setAutoReconnect(true);
  WiFi.begin(WIFI_SSID, WIFI_SENHA);

  unsigned long inicio = millis();

  while (WiFi.status() != WL_CONNECTED &&
         millis() - inicio < 30000) {
    delay(500);
    Serial.print('.');
  }

  if (WiFi.status() != WL_CONNECTED) {
    Serial.println("\nNao conectou. Confira a rede e a senha.");
    Serial.println("Depois de corrigir, grave novamente ou reinicie.");
    while (true) delay(1000);
  }

  Serial.println("\nWi-Fi conectado.");
  Serial.print("Abra no navegador: http://");
  Serial.println(WiFi.localIP());

  server.on("/", []() {
    server.send_P(200, "text/html; charset=utf-8", PAGINA);
  });

  server.on("/frame", HTTP_GET, enviarFrame);

  server.onNotFound([]() {
    server.send(404, "text/plain", "Pagina nao encontrada");
  });

  server.begin();
  Serial.println("Servidor web iniciado.");
}

void loop() {
  server.handleClient();
  delay(2);
}