# Boîte à outils Android

Application de bureau (Qt 6 / C++) qui regroupe, dans une interface simple, les commandes `adb` et `fastboot` les plus courantes : détection de l'appareil, diagnostic, sauvegarde des photos ou de la mémoire interne, déverrouillage / reverrouillage du bootloader, flashage d'images (Recovery, Système) et **flashage d'un dossier complet** (GrapheneOS, images d'origine Pixel, fonction expérimentale).
Projet personnel pour m'entraîner à la logique du C++, réalisé avec l'aide de l'IA pour la partie interface graphique (Qt).

> ⚠️ **Avertissement : utilisation à vos risques.**
> Le déverrouillage du bootloader **efface toutes les données** de l'appareil, et le flashage d'une image incorrecte peut le **rendre inutilisable** (« brick »). Cet outil est fourni **sans aucune garantie** et l'auteur ne peut être tenu responsable des dommages causés à votre appareil ou à vos données. Faites une sauvegarde avant toute opération et vérifiez que les fichiers que vous flashez correspondent exactement à votre modèle.

## Fonctionnalités

- **Détection** de l'appareil en mode fastboot
- **Diagnostic** : appareils connectés et état de la batterie (via ADB)
- **Sauvegarde des photos** (`/sdcard/DCIM/Camera/`)
- **Sauvegarde intégrale** de la mémoire interne (`/sdcard/`)
- **Déverrouillage / reverrouillage du bootloader**, avec confirmation et choix de la méthode (`fastboot flashing` ou `fastboot oem`)
- **Flash du Recovery** et **flash du Système** (fichiers `.img`)
- **Flash d'un dossier** contenant un script `flash-all.bat` : GrapheneOS ou image d'origine Pixel, avec extraction du `.zip` intégrée. **Fonction expérimentale, non testée sur appareil réel et désactivée par défaut** (voir ci-dessous)
- **Bouton Annuler** pour les opérations non critiques (sauvegardes, extraction, diagnostic)
- **Protections** : un seul traitement à la fois, vérification de la présence d'un appareil fastboot avant toute opération risquée, fermeture de la fenêtre refusée pendant un flash
- **Notifications n8n par webhook** (optionnel, désactivé par défaut)
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
| Déverrouiller / Reverrouiller | Fastboot | **Efface toutes les données**. Choisissez la méthode dans la liste déroulante |
| Flash Recovery / Système | Fastboot + bootloader déverrouillé | Choisissez un fichier `.img` adapté à votre modèle |
| Flash d'un dossier (expérimental) | Fastboot + bootloader déverrouillé | **Efface toutes les données**. Dossier extrait contenant `flash-all.bat`. À activer via la case « Activer les fonctions expérimentales » |
| Annuler | — | Indisponible pendant un flash ou un verrouillage (trop risqué) |

### Méthode de déverrouillage : `flashing` ou `oem`

- **`fastboot flashing unlock`** : méthode de la plupart des appareils récents (Pixel, etc.). C'est la valeur par défaut.
- **`fastboot oem unlock`** : méthode de certains anciens appareils, éventuellement avec un code fourni par le fabricant (champ « clé secrète », utilisé uniquement avec cette méthode).

Certains fabricants imposent leur propre procédure (outil dédié, compte, délai d'attente) : consultez la documentation de votre marque.

### Flasher GrapheneOS ou une image d'origine Pixel (fonction expérimentale)

> 🧪 **Fonction expérimentale.** Le flash d'un dossier n'a pas pu être testé sur un appareil réel par l'auteur. Il est donc **désactivé par défaut** : cochez la case **« Activer les fonctions expérimentales »** en haut du groupe « Flash d'un dossier » pour le débloquer. Le choix est mémorisé. Si vous l'essayez, vos retours sont précieux : ouvrez une *issue* avec le modèle de l'appareil, la version de `fastboot` et le journal affiché par l'application.

> Cette application n'est pas affiliée à GrapheneOS. Le [guide officiel](https://grapheneos.org/install/) et son [installeur web](https://grapheneos.org/install/web) restent la référence : ils sont régulièrement testés sur chaque système. Cet outil automatise la méthode « ligne de commande ».

1. Vérifiez que votre appareil est **officiellement supporté** (évitez les variantes opérateur, qui peuvent ne pas se déverrouiller).
2. Téléchargez le `.zip` officiel de votre appareil et **vérifiez sa signature** comme indiqué par le guide.
3. Déverrouillez le bootloader (méthode `flashing`), puis démarrez le téléphone en mode fastboot.
4. Dans le groupe **« Flash d'un dossier »**, cochez **« Activer les fonctions expérimentales »**, puis cliquez sur **« Extraire un .zip... »** (ou sélectionnez directement le dossier déjà extrait). Le dossier doit contenir `flash-all.bat`, un `image-*.zip` et un `bootloader-*.img`.
5. Cliquez sur **« Flasher le dossier »** et confirmez. L'opération dure plusieurs minutes ; ne débranchez rien et ne fermez pas l'application.
6. Suivez ensuite le guide officiel pour **reverrouiller le bootloader** (bouton « Reverrouiller le Bootloader »). Ne reverrouillez qu'un système signé et compatible avec le verrouillage.

Remarques :

- Une version **récente de `fastboot`** est exigée par GrapheneOS : gardez le dossier `platform-tools` à jour.
- Prévoyez de la place sur le disque : le zip et son extraction sont volumineux.
- L'application utilise le `fastboot` de son propre dossier `platform-tools` (ou celui du `PATH`) pour exécuter le script.

## Notifications n8n (webhooks, optionnel)

L'application peut envoyer des événements JSON à **votre propre** instance [n8n](https://n8n.io) (par exemple pour recevoir une notification Windows en cas d'erreur).

**Configuration :** groupe « Configuration n8n (Webhook) » → cochez *Activer les webhooks n8n*, saisissez l'URL (ex. `http://localhost:5678/webhook/android-app`) puis cliquez sur **Tester**. Le workflow n8n doit être **activé** pour utiliser l'URL `/webhook/` (l'URL `/webhook-test/` ne fonctionne qu'en écoute manuelle).

**Événements envoyés :**

| Événement | Quand | Contenu (`payload`) |
|---|---|---|
| `backup_completed` | Fin d'une sauvegarde | `device`, `size_mb`, `status` |
| `cmd_error` | Échec d'une commande adb/fastboot | `command`, `error_msg` |
| `device_connected` | Appareil vu par `adb devices` / `fastboot devices` | `serial`, `model` |
| `flash_completed` | Fin d'un flash réussi | `partition`, `status` (+ `image` pour un dossier) |

Chaque envoi contient aussi `event` et `timestamp` (ISO 8601, UTC).

**Vie privée :** la fonction est **désactivée par défaut** et aucune URL n'est codée en dur. Une fois activée, les données ci-dessus (numéro de série, modèle de l'appareil, commandes, messages d'erreur) sont envoyées **uniquement vers l'URL que vous avez saisie**. Les chemins personnels sont retirés des messages d'erreur. Si le serveur est injoignable, les événements sont conservés dans une file locale (500 maximum) et renvoyés plus tard.

**Sécurité :** le webhook n'a pas d'authentification. Gardez n8n en local (`localhost`) ; pour un serveur distant, utilisez HTTPS et protégez le webhook.

**Exemples :** le dossier [`examples/`](examples) contient `toast.ps1` (notification Windows) et un workflow n8n qui l'appelle en cas d'erreur. Adaptez le chemin `C:\scripts\toast.ps1` dans le nœud *Execute Command*. Ce nœud est désactivé par défaut dans certaines versions de n8n : autorisez-le avec la variable d'environnement `NODES_EXCLUDE="[]"` au lancement de n8n.

## Mises à jour automatiques

Au démarrage, l'application consulte le fichier [`updates.json`](updates.json) de ce dépôt. Si une version plus récente est publiée, une fenêtre propose de la télécharger.

## Compiler depuis les sources

**Prérequis :** Qt 6 (modules Widgets, Network et Concurrent), Qt Creator, CMake et un compilateur C++17 (MinGW ou MSVC). La bibliothèque [QSimpleUpdater](https://github.com/alex-spataru/QSimpleUpdater) est incluse dans le dossier `QSimpleUpdater/`.

1. Cloner le dépôt et l'ouvrir dans Qt Creator.
2. Choisir un kit Qt 6 et compiler en mode **Release**.
3. Placer le dossier `platform-tools` à côté de l'exécutable.
4. Pour distribuer l'application, utiliser `windeployqt` puis créer l'installeur avec [Inno Setup](https://jrsoftware.org/isinfo.php).

## Publier une nouvelle version (mainteneur)

1. Modifier la version dans `CMakeLists.txt` (`project(... VERSION x.y.z)`) : elle est transmise au code et affichée dans le titre de la fenêtre. Mettre aussi à jour `AppVersion` dans le script Inno Setup (en gardant le même `AppId`).
2. Recompiler en Release et générer l'installeur.
3. Créer une release GitHub `vX.Y.Z` et y déposer `BoiteAOutils-setup.exe`.
4. Mettre à jour `latest-version`, `download-url` et `changelog` dans `updates.json`, **en dernier**, une fois l'installeur en ligne.

## Licences

- Ce projet est distribué sous licence **MIT** (voir [LICENSE](LICENSE)).
- Il utilise le framework **Qt**, sous licence LGPL v3 : les bibliothèques Qt sont fournies sous forme de DLL dynamiques. Voir [qt.io/licensing](https://www.qt.io/licensing/).
- Il utilise **QSimpleUpdater** (licence MIT).
- Les **Android Platform-Tools** (`adb`, `fastboot`) appartiennent à Google et sont distribués sous licence Apache 2.0.

## Contribuer

Les suggestions et corrections sont les bienvenues : ouvrez une *issue* ou une *pull request*.
