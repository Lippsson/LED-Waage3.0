import 'package:flutter/material.dart';

import '../l10n.dart';
import '../screens/about_screen.dart';

/// AppBar action that opens the Info page (version, privacy, licences).
class AboutButton extends StatelessWidget {
  const AboutButton({super.key});

  @override
  Widget build(BuildContext context) {
    return IconButton(
      tooltip: tr('about_title'),
      icon: const Icon(Icons.info_outline),
      onPressed: () => Navigator.of(context).push(MaterialPageRoute<void>(builder: (_) => const AboutScreen())),
    );
  }
}
