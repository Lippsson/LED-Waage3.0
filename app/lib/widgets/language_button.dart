import 'package:flutter/material.dart';

import '../l10n.dart';

/// AppBar action to pick the app language.
class LanguageButton extends StatelessWidget {
  const LanguageButton({super.key});

  @override
  Widget build(BuildContext context) {
    return PopupMenuButton<String>(
      tooltip: tr('language'),
      icon: const Icon(Icons.language),
      initialValue: appLang.value,
      onSelected: setLanguage,
      itemBuilder: (_) => [
        for (final e in supportedLanguages.entries)
          CheckedPopupMenuItem<String>(
            value: e.key,
            checked: e.key == appLang.value,
            child: Text(e.value),
          ),
      ],
    );
  }
}
