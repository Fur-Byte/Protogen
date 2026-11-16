// The core advances logical blink state. The shell retains the scheduling
// clock read at the original service point after an ordinary blink ends.
void Blink(uint32_t now) {
  if (behavior.advanceBlink(now)) scheduleBlink();
}
