# Boîte à outils Android

Application de bureau (Qt / C++) qui regroupe, dans une interface simple, les commandes `adb` et `fastboot` les plus courantes : détection de l'appareil, diagnostic, sauvegarde des photos ou de la mémoire interne, déverrouillage du bootloader et flashage d'images (Recovery, Système).

> ⚠️ **Avertissement : utilisation à vos risques.**
> Le déverrouillage du bootloader **efface toutes les données** de l'appareil, et le flashage d'une image incorrecte peut le **rendre inutilisable** (« brick »). Cet outil est fourni **sans aucune garantie** et l'auteur ne peut être tenu responsable des dommages causés à votre appareil ou à vos données. Faites une sauvegarde avant toute opération et vérifiez que les fichiers que vous flashez correspondent exactement à votre modèle.

## Fonctionnalités

- **Détection** de l'appareil en mode fastboot
- **Diagnostic** : appareils connectés et état de la batterie (via ADB)
- **Sauvegarde des photos** (`/sdcard/DCIM/Camera/`)
- **Sauvegarde intégrale** de la mémoire interne (`/sdcard/`)
- **Déverrouillage du bootloader** (avec confirmation et code optionnel)
- **Flash du Recovery** et **flash du Système** (fichiers `.img`)
- **Mises à jour automatiques** : l'application vérifie au lancement si une nouvelle version est disponible

## Installation (Windows)

1. Rendez-vous sur la page des [**Releases**](https://github.com/Noalmdt/boite_a_outils_android/releases).
2. Téléchargez `BoiteAOutils-setup.exe` de la dernière version.
3. Lancez l'installeur et suivez les étapes.

> Windows SmartScreen peut afficher « Windows a protégé votre ordinateur », car l'installeur n'est pas signé. Cliquez sur **Informations complémentaires → Exécuter quand même**.

Les outils `adb` et `fastboot` (Android Platform-Tools) sont inclus dans le dossier `platform-tools` de l'installation. Si `adb` ou `fastboot` sont déjà dans votre `PATH`, ce sont ceux-là qui seront utilisés en priorité.

## Prérequis côté téléphone

1. **Activer les options pour les développeurs** : *Paramètres → À propos du téléphone*, puis appuyer plusieurs fois sur le numéro de build.
2. **Activer le débogage USB** (pour les fonctions ADB : diagnostic, sauvegardes).
3. **Autoriser le déverrouillage OEM** dans les options pour les développeurs (pour le déverrouillage du bootloader).
4. Installer le **pilote USB** de votre fabricant si Windows ne reconnaît pas l'appareil.
5. Utiliser un **câble USB de qualité** et un port directement sur le PC (évitez les hubs).
6. Pour les fonctions fastboot, démarrer le téléphone en **mode fastboot / bootloader** (la combinaison de touches dépend du modèle).

## Utilisation

| Action | Mode requis | Remarques |
|---|---|---|
| Détecter | Fastboot | Affiche les appareils vus par `fastboot devices` |
| Diagnostic | Système + débogage USB | Autoriser l'ordinateur sur le téléphone à la première connexion |
| Sauvegardes | Système + débogage USB | Le dossier de destination est configurable ; sinon `Documents\BoiteAOutilsAndroid` |
| Déverrouiller | Fastboot | **Efface toutes les données** |
| Flash Recovery / Système | Fastboot + bootloader déverrouillé | Choisissez un fichier `.img` adapté à votre modèle |

## Mises à jour automatiques

Au démarrage, l'application consulte le fichier [`updates.json`](updates.json) de ce dépôt. Si une version plus récente est publiée, une fenêtre propose de la télécharger.

## Compiler depuis les sources

**Prérequis :** Qt 6 (ou Qt 5), Qt Creator et un compilateur C++ (MinGW ou MSVC). La bibliothèque [QSimpleUpdater](https://github.com/alex-spataru/QSimpleUpdater) est incluse dans le dossier `QSimpleUpdater/`.

1. Cloner le dépôt et l'ouvrir dans Qt Creator.
2. Choisir un kit et compiler en mode **Release**.
3. Placer le dossier `platform-tools` à côté de l'exécutable.
4. Pour distribuer l'application, utiliser `windeployqt` puis créer l'installeur avec [Inno Setup](https://jrsoftware.org/isinfo.php).

## Publier une nouvelle version (mainteneur)

1. Modifier `APP_VERSION` dans `mainwindow.cpp`, recompiler en Release et générer l'installeur.
2. Créer une release GitHub `vX.Y.Z` et y déposer `BoiteAOutils-setup.exe`.
3. Mettre à jour `latest-version`, `download-url` et `changelog` dans `updates.json`, **en dernier**, une fois l'installeur en ligne.

## Licences

- Ce projet est distribué sous licence **MIT** (voir [LICENSE](LICENSE)).
- Il utilise le framework **Qt**, sous licence LGPL v3 : les bibliothèques Qt sont fournies sous forme de DLL dynamiques. Voir [qt.io/licensing](https://www.qt.io/licensing/).
- Il utilise **QSimpleUpdater** (licence MIT).
- Les **Android Platform-Tools** (`adb`, `fastboot`) appartiennent à Google et sont distribués sous licence Apache 2.0.

## Contribuer

Les suggestions et corrections sont les bienvenues : ouvrez une *issue* ou une *pull request*.
