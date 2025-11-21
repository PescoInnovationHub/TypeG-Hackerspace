#include <Adafruit_NeoPixel.h>

// **************** CONFIGURAZIONE NEOPIXEL ****************
#define PIN            4      // Il pin D2 sull'ESP8266 è GPIO4.
#define NUMPIXELS      30      // Numero di NeoPixel
#define FADE_DELAY     10     // Tempo di attesa in ms tra ogni "passo" della dissolvenza (più basso = più veloce/fluido)
#define STEPS          100    // Numero di passi (frames) per passare da un colore all'altro (più alto = più fluido)

// Creazione dell'oggetto Adafruit_NeoPixel
Adafruit_NeoPixel pixels(NUMPIXELS, PIN, NEO_GRB + NEO_KHZ800);

// **************** DEFINIZIONE DEI COLORI ****************
// Nota: Usiamo l'array di colori per definire la sequenza.
// RGB è più semplice per i colori non-standard.
// Ho usato luminosità 100 per un fade più morbido.
const uint32_t colorSequence[] = {
  pixels.Color(255, 255, 255), // 0. BIANCO (Max luminosità)
  pixels.Color(255, 0, 0),     // 1. ROSSO
  pixels.Color(128, 0, 255),   // 2. VIOLA (Più blu del Magenta)
  pixels.Color(0, 0, 255),     // 3. BLU
  pixels.Color(0, 255, 0),     // 4. VERDE
  pixels.Color(255, 255, 0)     // 5. GIALLO
};
const int NUM_COLORS = sizeof(colorSequence) / sizeof(colorSequence[0]);

// Funzione per estrarre la componente (R, G, o B) da un colore uint32_t
uint8_t getR(uint32_t c) { return (uint8_t)(c >> 16); }
uint8_t getG(uint32_t c) { return (uint8_t)(c >> 8); }
uint8_t getB(uint32_t c) { return (uint8_t)(c); }

/**
 * Funzione di interpolazione lineare (lerp) per i colori.
 * Calcola il colore intermedio tra due colori (c1 e c2) in base al passo (step).
 */
uint32_t interpolateColor(uint32_t c1, uint32_t c2, int step, int totalSteps) {
  float ratio = (float)step / totalSteps;
  
  uint8_t r = getR(c1) + (uint8_t)((getR(c2) - getR(c1)) * ratio);
  uint8_t g = getG(c1) + (uint8_t)((getG(c2) - getG(c1)) * ratio);
  uint8_t b = getB(c1) + (uint8_t)((getB(c2) - getB(c1)) * ratio);
  
  return pixels.Color(r, g, b);
}


void setup() {
  pixels.begin(); 
  pixels.setBrightness(150); // Luminosità media per un fade migliore
  pixels.clear(); 
  pixels.show(); 
}

void loop() {
  
  // Ciclo attraverso tutti i colori definiti nella sequenza
  for (int i = 0; i < NUM_COLORS; i++) {
    
    // Il colore di PARTENZA è il colore corrente nella sequenza
    uint32_t cFrom = colorSequence[i];
    
    // Il colore di ARRIVO è il colore successivo. 
    // Usiamo l'operatore modulo (%) per tornare al primo colore dopo l'ultimo (ciclo infinito).
    uint32_t cTo = colorSequence[(i + 1) % NUM_COLORS];
    
    // Esegui la dissolvenza (fade) da cFrom a cTo
    for (int step = 0; step <= STEPS; step++) {
      
      // Calcola il colore intermedio
      uint32_t currentColor = interpolateColor(cFrom, cTo, step, STEPS);
      
      // Imposta tutti i pixel al colore intermedio
      pixels.fill(currentColor);
      
      // Invia il colore
      pixels.show();
      
      // Breve pausa per rendere visibile il passo
      delay(FADE_DELAY);
    }
  }
}