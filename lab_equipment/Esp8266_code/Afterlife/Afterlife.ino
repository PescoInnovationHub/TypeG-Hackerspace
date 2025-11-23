#include <Adafruit_NeoPixel.h>

// **************** CONFIGURAZIONE NEOPIXEL AFTERLIFE****************
#define PIN            4      // Il pin D2 sull'ESP8266 è GPIO4.
#define NUMPIXELS      40      // Numero di NeoPixel
#define FADE_DELAY     120     // Tempo di attesa in ms tra ogni "passo" della dissolvenza (più basso = più veloce/fluido)
#define STEPS          350    // Numero di passi (frames) per passare da un colore all'altro (più alto = più fluido)

// Creazione dell'oggetto Adafruit_NeoPixel
Adafruit_NeoPixel pixels(NUMPIXELS, PIN, NEO_GRB + NEO_KHZ800);

// **************** DEFINIZIONE DEI COLORI ****************
// Nota: Usiamo l'array di colori per definire la sequenza.
// RGB è più semplice per i colori non-standard.
// Ho usato luminosità 220 per un fade brillante.
// ...existing code...
const uint32_t colorSequence[] = {
  pixels.Color(0, 255, 255),    // CIANO NEON
  pixels.Color(0, 255, 128),    // VERDE ACIDO
  pixels.Color(0, 128, 255),    // TURCHESE VIVO
  pixels.Color(0, 0, 255),      // BLU ELETTRICO
  pixels.Color(64, 0, 255),     // BLU VIOLA NEON
  pixels.Color(128, 0, 255),    // VIOLA NEON
  pixels.Color(192, 0, 255),    // VIOLA SATURO
  pixels.Color(255, 0, 255),    // MAGENTA NEON
  pixels.Color(255, 0, 192),    // MAGENTA ROSA
  pixels.Color(255, 0, 128),    // ROSA FLUO
  pixels.Color(255, 64, 128),   // ROSA ARANCIO
  pixels.Color(255, 128, 255),  // LILLA NEON
  pixels.Color(255, 128, 0),    // ARANCIONE FLUO
  pixels.Color(255, 255, 0),    // GIALLO LASER
  pixels.Color(192, 255, 0),    // GIALLO VERDE NEON
  pixels.Color(128, 255, 0),    // LIME FLUO
  pixels.Color(0, 255, 64),     // VERDE LIME NEON
  pixels.Color(0, 255, 192),    // VERDE ACQUA NEON
  pixels.Color(0, 128, 128),    // VERDE BLU NEON
  pixels.Color(128, 255, 255),  // AZZURRO CHIARO NEON
  pixels.Color(255, 255, 255),  // BIANCO NEON
  pixels.Color(255, 215, 0),    // ORO
  pixels.Color(192, 192, 192),  // ARGENTO
  pixels.Color(0, 0, 128),      // BLU SCURO
  pixels.Color(128, 0, 128)     // VIOLA SCURO
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
  pixels.setBrightness(220); // Luminosità intensa, stile club Afterlife
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