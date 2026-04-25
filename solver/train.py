import pandas as pd
from sklearn.ensemble import GradientBoostingClassifier
from sklearn.model_selection import train_test_split
from sklearn.metrics import accuracy_score, classification_report, roc_auc_score
import joblib

DATASET_FILE = "dataset.csv"
MODEL_FILE = "model.pkl"
WEIGHTS_FILE = "model_weights.txt"

FEATURES = [
    "area",
    "ratio",
    "efficiency",
    "gap_ratio",
    "angle",
    "x",
    "y",
    "n_bays",
    "used_area_ratio",
    "valid",
]

df = pd.read_csv(DATASET_FILE)

# Limpieza básica
df = df.dropna()
df = df[df["valid"].isin([0, 1])]
df = df[df["label"].isin([0, 1])]

X = df[FEATURES]
y = df["label"]

print("Filas dataset:", len(df))
print("Labels:")
print(y.value_counts())

if y.nunique() < 2:
    raise ValueError("El dataset solo tiene una clase. Necesitas ejemplos con label=0 y label=1.")

X_train, X_test, y_train, y_test = train_test_split(
    X,
    y,
    test_size=0.2,
    random_state=42,
    stratify=y
)

model = GradientBoostingClassifier(
    n_estimators=150,
    learning_rate=0.05,
    max_depth=3,
    random_state=42
)

model.fit(X_train, y_train)

pred = model.predict(X_test)
proba = model.predict_proba(X_test)[:, 1]

print("\nAccuracy:", accuracy_score(y_test, pred))
print("ROC AUC:", roc_auc_score(y_test, proba))
print("\nReport:")
print(classification_report(y_test, pred))

print("\nFeature importances:")
for name, value in zip(FEATURES, model.feature_importances_):
    print(f"{name}: {value:.6f}")

joblib.dump(model, MODEL_FILE)

with open(WEIGHTS_FILE, "w") as f:
    for value in model.feature_importances_:
        f.write(str(value) + "\n")

print(f"\nModelo guardado en: {MODEL_FILE}")
print(f"Pesos guardados en: {WEIGHTS_FILE}")