const { withAppDelegate, withInfoPlist } = require("expo/config-plugins");

const reactNativeStartup = `#if os(iOS) || os(tvOS)
    window = UIWindow(frame: UIScreen.main.bounds)
    factory.startReactNative(
      withModuleName: "main",
      in: window,
      launchOptions: launchOptions)
#endif`;

const sceneDelegate = `@objc(SceneDelegate)
class SceneDelegate: ExpoAppSceneDelegate {}`;

module.exports = function withIOSSceneLifecycle(config) {
  config = withAppDelegate(config, (config) => {
    if (config.modResults.language !== "swift") {
      throw new Error("The iOS scene lifecycle plugin requires a Swift AppDelegate.");
    }

    let contents = config.modResults.contents.replace(
      "class AppDelegate: ExpoAppDelegate {",
      "class AppDelegate: ExpoAppDelegate, ExpoReactNativeFactoryProvider {"
    );
    contents = contents.replace(reactNativeStartup, "");

    if (!contents.includes("ExpoReactNativeFactoryProvider")) {
      throw new Error("Could not configure the Expo React Native factory provider.");
    }
    if (!contents.includes("class SceneDelegate: ExpoAppSceneDelegate {}")) {
      contents = contents.replace(
        "class ReactNativeDelegate:",
        `${sceneDelegate}\n\nclass ReactNativeDelegate:`
      );
    }
    if (!contents.includes("class SceneDelegate: ExpoAppSceneDelegate {}")) {
      throw new Error("Could not add the Expo scene delegate to AppDelegate.swift.");
    }

    config.modResults.contents = contents;
    return config;
  });

  return withInfoPlist(config, (config) => {
    config.modResults.UIApplicationSceneManifest = {
      UIApplicationSupportsMultipleScenes: false,
      UISceneConfigurations: {
        UIWindowSceneSessionRoleApplication: [
          {
            UISceneConfigurationName: "Default Configuration",
            UISceneDelegateClassName: "$(PRODUCT_MODULE_NAME).SceneDelegate",
          },
        ],
      },
    };
    return config;
  });
};
