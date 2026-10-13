import 'package:flutter/material.dart';
import 'package:url_launcher/url_launcher.dart';

import '../l10n.dart';
import '../legal.dart';

class AboutScreen extends StatelessWidget {
  const AboutScreen({super.key});

  Future<void> _open(BuildContext context, String url) async {
    final messenger = ScaffoldMessenger.of(context);
    var ok = false;
    try {
      ok = await launchUrl(Uri.parse(url), mode: LaunchMode.externalApplication);
    } catch (_) {}
    if (!ok) {
      messenger
        ..hideCurrentSnackBar()
        ..showSnackBar(SnackBar(content: Text(tr('about_open_failed'))));
    }
  }

  Widget _linkCard(BuildContext context, String title, String text, String url) {
    return Card(
      child: InkWell(
        borderRadius: BorderRadius.circular(12),
        onTap: () => _open(context, url),
        child: Padding(
          padding: const EdgeInsets.all(14),
          child: Column(
            crossAxisAlignment: CrossAxisAlignment.start,
            children: [
              Text(title, style: const TextStyle(fontWeight: FontWeight.w600)),
              const SizedBox(height: 4),
              Text(text),
              const SizedBox(height: 6),
              Row(
                children: [
                  const Icon(Icons.open_in_new, size: 16),
                  const SizedBox(width: 6),
                  Expanded(
                    child: Text(url.replaceFirst('https://', ''),
                        style: TextStyle(color: Theme.of(context).colorScheme.primary)),
                  ),
                ],
              ),
            ],
          ),
        ),
      ),
    );
  }
  @override
  Widget build(BuildContext context) {
    return ValueListenableBuilder<String>(
      valueListenable: appLang,
      builder: (context, _, __) => Scaffold(
        appBar: AppBar(title: Text(tr('about_title'))),
        body: ListView(
          padding: const EdgeInsets.all(16),
          children: [
            Text(tr('app_name'), style: Theme.of(context).textTheme.headlineSmall),
            Text('${tr('about_version')} $appVersion'),
            const SizedBox(height: 16),
            Text(tr('about_text')),
            const SizedBox(height: 16),
            Card(
              child: Padding(
                padding: const EdgeInsets.all(14),
                child: Column(
                  crossAxisAlignment: CrossAxisAlignment.start,
                  children: [
                    Text(tr('about_privacy_title'), style: const TextStyle(fontWeight: FontWeight.w600)),
                    const SizedBox(height: 4),
                    Text(tr('about_privacy')),
                  ],
                ),
              ),
            ),
            const SizedBox(height: 8),
            _linkCard(context, tr('about_contact_title'), tr('about_contact'), repoUrl),
            const SizedBox(height: 8),
            _linkCard(context, tr('about_origin_title'), tr('about_origin'), upstreamUrl),
            const SizedBox(height: 8),
            if (legalNotice.trim().isNotEmpty)
              Card(
                child: Padding(
                  padding: const EdgeInsets.all(14),
                  child: Column(
                    crossAxisAlignment: CrossAxisAlignment.start,
                    children: [
                      Text(tr('about_legal'), style: const TextStyle(fontWeight: FontWeight.w600)),
                      const SizedBox(height: 4),
                      SelectableText(legalNotice),
                    ],
                  ),
                ),
              ),
            const SizedBox(height: 8),
            OutlinedButton.icon(
              onPressed: () => showLicensePage(
                context: context,
                applicationName: tr('app_name'),
                applicationVersion: appVersion,
                applicationLegalese: tr('about_legalese'),
              ),
              icon: const Icon(Icons.description_outlined),
              label: Text(tr('about_licenses')),
            ),
            const SizedBox(height: 8),
            Text(tr('about_licenses_hint'), style: const TextStyle(fontSize: 12)),
          ],
        ),
      ),
    );
  }
}
