void setup() {
  Serial.begin(9600);
}

bool active = false;
elapsedMillis activeMillis;
int peak = 0;
elapsedMillis peakMillis;

void loop() {
  const auto value = analogRead(A0);
//#if 0
  peak = max(peak, value);
  if (peakMillis > 20) {
    peakMillis = 0;
    Serial.println(peak);
    peak = 0;
  }
  delay(10);
//#endif
#if 0
  if (active) {
    Serial.println(value);

    if (activeMillis > 500) {
      active = false;
    }
  }
  else if (value > 200) {
    active = true;
    activeMillis = 0;
  }
#endif
}
