import Lean

namespace Protogen

-- Max72xxPanel::drawPixel, for the configured horizontal 14-tile panel.
theorem tileIndex (x y : Nat) (hx : x < 112) (hy : y < 8) :
    x / 8 + 14 * (y / 8) < 14 := by omega

theorem rotatedTileIndex (x y : Nat) (hx : x < 8) (hy : y < 112) :
    (111 - y) / 8 + 14 * (x / 8) < 14 := by omega

theorem pixelIndex (tile column : Nat) (ht : tile < 14) :
    tile * 8 + column % 8 < 112 := by omega

theorem localRotation (x y : Nat) (hx : x < 8) (hy : y < 8) :
    7 - y < 8 ∧ x < 8 ∧ 7 - x < 8 ∧ 7 - (7 - x) = x ∧ 7 - (7 - y) = y := by omega

theorem rotatedTileIndex180 (x y : Nat) (hx : x < 112) (hy : y < 8) :
    (111 - x) / 8 + 14 * ((7 - y) / 8) < 14 := by omega

theorem rotatedTileIndex270 (x y : Nat) (hx : x < 8) (hy : y < 112) :
    y / 8 + 14 * ((7 - x) / 8) < 14 := by omega

-- ActionQueue: abstract count/index model, not a proof of the C++ queue.
theorem enqueueCapacity (count : Nat) (h : count < 4) : count + 1 ≤ 4 := by omega
theorem dequeueCapacity (count : Nat) (h : count ≤ 4) (nonempty : 0 < count) :
    count - 1 < 4 := by omega
theorem nextSlot (index : Nat) : (index + 1) % 4 < 4 := by omega

-- RetryDelay and ReleaseGate: unsigned 32-bit subtraction across rollover.
theorem elapsedModulo (start elapsed : BitVec 32) :
    start + elapsed - start = elapsed := by bv_decide

theorem remainingBound (elapsed delay : Nat) : delay - elapsed ≤ delay := by omega

-- RetryDelay::ready: an expired or reset delay stays disarmed until start.
def retryPending (pending : Bool) (elapsed delay : Nat) : Bool :=
  pending && decide (elapsed < delay)

theorem retryExpires (elapsed delay : Nat) (h : delay ≤ elapsed) :
    retryPending true elapsed delay = false := by
  simp [retryPending, Nat.not_lt.mpr h]

theorem retryStaysDisarmed (elapsed delay : Nat) :
    retryPending false elapsed delay = false := by rfl

-- ReplayCore: age monotonicity, independently of the bitmap representation.
-- The CBMC step harness checks the concrete bitmap transition.
theorem expiredStaysExpired (old top newer : Nat) (h : old ≤ top)
    (expired : 64 ≤ top - old) (advance : top ≤ newer) :
    64 ≤ newer - old := by omega

theorem acceptedSequenceAge (old top : Nat)
    (fresh : top - old < 64) : top - old ≤ 63 := by omega

-- HelmetProtocol::u16/put16: base-256 representation, with byte bounds.
theorem littleEndian16 (value : Nat) (h : value < 65536) :
    value % 256 + 256 * (value / 256) = value ∧ value / 256 < 256 := by omega

-- SensorMath::signedCurrent: mathematical 20-bit sign extension.
def signed20 (raw : Int) : Int := if raw ≥ 524288 then raw - 1048576 else raw

theorem signedCurrentRange (raw : Int) (lo : 0 ≤ raw) (hi : raw < 1048576) :
    -524288 ≤ signed20 raw ∧ signed20 raw ≤ 524287 := by
  unfold signed20
  split <;> omega

-- Absolute sample/amplitude bounds; the real floating-point bias/filter still
-- needs separate verification before applying the energy bound to that code.
theorem amplitudeAccumulator (samples amplitude : Nat)
    (hs : samples ≤ 10) (ha : amplitude ≤ 1023) :
    samples * amplitude ≤ 10230 := by
  calc
    samples * amplitude ≤ 10 * amplitude := Nat.mul_le_mul_right _ hs
    _ ≤ 10230 := by omega

theorem audioWindow (samples amplitude : Nat)
    (hs : samples ≤ 64) (ha : amplitude ≤ 1023) :
    samples * amplitude ≤ 65535 := by
  calc
    samples * amplitude ≤ 64 * amplitude := Nat.mul_le_mul_right _ hs
    _ ≤ 65535 := by omega

theorem audioRing (index : Nat) : (index + 1) % 32 < 32 := by omega

-- Timeline::readEvent, after validAsset's event-count and total-size checks.
theorem timelineEventBounds (index : Nat) (h : index < 32) :
    16 + index * 10 + 9 < 336 := by omega

-- ConfigStore current 17-field layout: 12-byte header, 34-byte fields,
-- 2-byte brightness approval, 4-byte CRC, and final commit marker.
theorem configFieldBounds (slot index : Nat) (hs : slot < 2) (hi : index < 17) :
    slot * 64 + 12 + index * 2 + 1 < 128 := by omega

theorem configMarkerBounds (slot : Nat) (hs : slot < 2) :
    slot * 64 + 52 < 128 := by omega

-- RuntimeConfig::effectiveBrightness: approval and hardware ceiling.
def brightness (requested approval : Nat) := min (min requested approval) 1

theorem brightnessBound (requested approval : Nat) :
    brightness requested approval ≤ 1 ∧ brightness requested approval ≤ approval := by
  unfold brightness
  omega

-- SecureStorage::save result model. The first flag means the encoded value
-- passed validation. Indeterminate includes ambiguous errors and no readback.
inductive SaveOutcome where
  | notWritten | indeterminate | verified
  deriving DecidableEq

def saveOutcome (valid set commit readback ambiguous : Bool) : SaveOutcome :=
  if !valid then .notWritten
  else if set then
    if commit && readback then .verified else .indeterminate
  else if ambiguous then .indeterminate else .notWritten

theorem verifiedRequiresReadback (valid set commit readback ambiguous : Bool)
    (h : saveOutcome valid set commit readback ambiguous = .verified) :
    valid = true ∧ set = true ∧ commit = true ∧ readback = true := by
  cases valid <;> cases set <;> cases commit <;> cases readback <;>
    cases ambiguous <;> simp [saveOutcome] at h ⊢

theorem invalidNotWritten (set commit readback ambiguous : Bool) :
    saveOutcome false set commit readback ambiguous = .notWritten := by rfl

-- An abstract replay history records all accepted values in one session.
-- The finite bitmap implementation is checked independently by CBMC.
def replayAllowed (top : Nat) (history : List Nat) (seq : Nat) : Prop :=
  0 < seq ∧ seq ≤ 9223372036854775807 ∧
    (top < seq ∨ (top - seq < 64 ∧ seq ∉ history))

theorem acceptedHistoryRejected (top seq : Nat) (history : List Nat)
    : ¬ replayAllowed (max top seq) (seq :: history) seq := by
  unfold replayAllowed
  intro rejected
  rcases rejected with ⟨_, _, future | ⟨_, unseen⟩⟩
  · omega
  · simp at unseen

-- Session identity is an explicit model input, not a proved cryptographic fact.
def sessionAllowed (active incoming : Nat) (allowed : Prop) : Prop :=
  active = incoming ∧ allowed

theorem oldSessionRejected (active old : Nat) (allowed : Prop)
    (different : active ≠ old) : ¬ sessionAllowed active old allowed := by
  simp [sessionAllowed, different]

#print axioms tileIndex
#print axioms rotatedTileIndex
#print axioms rotatedTileIndex180
#print axioms rotatedTileIndex270
#print axioms pixelIndex
#print axioms localRotation
#print axioms enqueueCapacity
#print axioms dequeueCapacity
#print axioms nextSlot
#print axioms elapsedModulo
#print axioms remainingBound
#print axioms retryExpires
#print axioms retryStaysDisarmed
#print axioms expiredStaysExpired
#print axioms acceptedSequenceAge
#print axioms littleEndian16
#print axioms signedCurrentRange
#print axioms amplitudeAccumulator
#print axioms audioWindow
#print axioms audioRing
#print axioms timelineEventBounds
#print axioms configFieldBounds
#print axioms configMarkerBounds
#print axioms brightnessBound
#print axioms verifiedRequiresReadback
#print axioms invalidNotWritten
#print axioms acceptedHistoryRejected
#print axioms oldSessionRejected

end Protogen
