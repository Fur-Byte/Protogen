// Matrix writing expressions

void drawExpression() {
  switch (state) {
    case ExpressionHappy: // 0: Happy expression
      matrix.drawBitmap(EyeLeft, 0, EyeL, 16, 8, HIGH);
      matrix.drawBitmap(EyeRight, 0, Eye, 16, 8, HIGH);
      break;

    case ExpressionSurprised: // 1: Surprised expression
      matrix.drawBitmap(EyeLeft, 0, SpookedL, 16, 8, HIGH);
      matrix.drawBitmap(EyeRight, 0, Spooked, 16, 8, HIGH);
      break;

    case ExpressionAngry: // 2: Angry expression
      matrix.drawBitmap(EyeLeft, 0, AngryL, 16, 8, HIGH);
      matrix.drawBitmap(EyeRight, 0, Angry, 16, 8, HIGH);
      break;

    case ExpressionVwv: // 3: vwv expression
      matrix.drawBitmap(EyeLeft, 0, vwvL, 16, 8, HIGH);
      matrix.drawBitmap(EyeRight, 0, vwv, 16, 8, HIGH);
      break;

    default:
      state = ExpressionHappy;
      matrix.drawBitmap(EyeLeft, 0, EyeL, 16, 8, HIGH);
      matrix.drawBitmap(EyeRight, 0, Eye, 16, 8, HIGH);
      break;
  }
  rising = false;
}
