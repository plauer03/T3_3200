// --- EINSTELLUNGEN ---
const int PWM_MAX_TICKS = 4200; 
int MY_POWER = 2000;      // 28%

// WICHTIG: Da wir jetzt echtes Floating nutzen, ändert sich der magnetische Winkel leicht. 
// Es kann gut sein, dass du deinen OFFSET neu ermitteln musst (z.B. 1, 3 oder 4 statt 2).
const int OFFSET = 2; 

// --- PINS ---
// HIN (High-Side, PWM-Pins, über Register gesteuert. PC2, PC4, PC6 entsprechen Pin 34, 36, 38)
const int PWM_PIN_U = 0; 
const int PWM_PIN_V = 1; 
const int PWM_PIN_W = 2; 

// LIN* (Low-Side, normale Digital-Pins. Passe diese an deine Verkabelung an!)
const int LIN_PIN_U = 35;
const int LIN_PIN_V = 37;
const int LIN_PIN_W = 39;

// --- ZUSTÄNDE ---
#define PHASE_HIGH  1  // High-Side AN (PWM), Low-Side AUS
#define PHASE_LOW  -1  // High-Side AUS, Low-Side AN (GND)
#define PHASE_FLOAT 0  // BEIDE AUS (Schwebend)

const int hallToStep[8] = { -1, 0, 2, 1, 4, 5, 3, -1 };

void setup() {
  Serial.begin(115200);

  pinMode(A0, INPUT_PULLUP);
  pinMode(A1, INPUT_PULLUP);
  pinMode(A2, INPUT_PULLUP);

  // LIN* Pins als Output setzen
  pinMode(LIN_PIN_U, OUTPUT);
  pinMode(LIN_PIN_V, OUTPUT);
  pinMode(LIN_PIN_W, OUTPUT);

  // SICHERHEIT: Alle Phasen sofort auf Floating schalten!
  digitalWrite(LIN_PIN_U, HIGH);
  digitalWrite(LIN_PIN_V, HIGH);
  digitalWrite(LIN_PIN_W, HIGH);

  // Hardware PWM Setup (Dein super-schneller Code)
  pmc_enable_periph_clk(ID_PIOC);
  pmc_enable_periph_clk(ID_PWM);
  PIOC->PIO_ABSR |= PIO_PC2 | PIO_PC4 | PIO_PC6; 
  PIOC->PIO_PDR  = PIO_PC2 | PIO_PC4 | PIO_PC6; 
  PWM->PWM_CLK = PWM_CLK_PREA(0) | PWM_CLK_DIVA(1);

  configurePWM(PWM_PIN_U);
  configurePWM(PWM_PIN_V);
  configurePWM(PWM_PIN_W);
  PWM->PWM_ENA = PWM_ENA_CHID0 | PWM_ENA_CHID1 | PWM_ENA_CHID2;
}

void configurePWM(int ch) {
  PWM->PWM_CH_NUM[ch].PWM_CMR  = PWM_CMR_CPRE_CLKA;
  PWM->PWM_CH_NUM[ch].PWM_CPRD = PWM_MAX_TICKS;
  PWM->PWM_CH_NUM[ch].PWM_CDTY = 0; 
}

// NEU: Diese Funktion steuert HIN (PWM) und LIN* (Digital) für eine einzelne Phase
void setSinglePhase(int pwmCh, int linPin, int state) {
  if (state == PHASE_HIGH) {
    digitalWrite(linPin, HIGH);                     // Zuerst Low-Side sicher AUS
    PWM->PWM_CH_NUM[pwmCh].PWM_CDTYUPD = MY_POWER;  // Dann High-Side PWM AN
  } 
  else if (state == PHASE_LOW) {
    PWM->PWM_CH_NUM[pwmCh].PWM_CDTYUPD = 0;         // Zuerst High-Side PWM AUS
    digitalWrite(linPin, LOW);                      // Dann Low-Side AN (zieht auf GND)
  } 
  else { // PHASE_FLOAT
    PWM->PWM_CH_NUM[pwmCh].PWM_CDTYUPD = 0;         // High-Side AUS
    digitalWrite(linPin, HIGH);                     // Low-Side AUS
  }
}

// Wrapper-Funktion für alle drei Phasen gleichzeitig
void applyPhases(int stateU, int stateV, int stateW) {
  setSinglePhase(PWM_PIN_U, LIN_PIN_U, stateU);
  setSinglePhase(PWM_PIN_V, LIN_PIN_V, stateV);
  setSinglePhase(PWM_PIN_W, LIN_PIN_W, stateW);
}

// NEU: Echte 6-Schritt Blockkommutierung (immer 1x High, 1x Low, 1x Float)
void setStep(int step) {
  step = step % 6;
  if (step < 0) step += 6;

  switch (step) {
    case 0: applyPhases(PHASE_HIGH, PHASE_LOW,   PHASE_FLOAT); break; // U -> V
    case 1: applyPhases(PHASE_HIGH, PHASE_FLOAT, PHASE_LOW);   break; // U -> W
    case 2: applyPhases(PHASE_FLOAT,PHASE_HIGH,  PHASE_LOW);   break; // V -> W
    case 3: applyPhases(PHASE_LOW,  PHASE_HIGH,  PHASE_FLOAT); break; // V -> U
    case 4: applyPhases(PHASE_LOW,  PHASE_FLOAT, PHASE_HIGH);  break; // W -> U
    case 5: applyPhases(PHASE_FLOAT,PHASE_LOW,   PHASE_HIGH);  break; // W -> V
  }
}

void loop() {
  // 1. Hall lesen
  int hA = digitalRead(A0); 
  int hB = digitalRead(A2);
  int hC = digitalRead(A1);
  
  int hallState = (hA << 2) | (hB << 1) | hC;

  // 2. Hall Code in Schritt-Nummer umwandeln (0-5)
  int currentStep = hallToStep[hallState];

  // 3. Fehlerbehandlung (z.B. alle Sensoren 0 oder 1)
  if (currentStep == -1) {
    // WICHTIG: Vorher hast du (0,0,0) gesendet, was eine Vollbremsung war (alle 3 auf GND). 
    // Jetzt nutzen wir echtes Freilaufen (Floating).
    applyPhases(PHASE_FLOAT, PHASE_FLOAT, PHASE_FLOAT);
    return;
  }

  // 4. OFFSET ADDIEREN
  setStep(currentStep + OFFSET);
  
  delayMicroseconds(10);
}