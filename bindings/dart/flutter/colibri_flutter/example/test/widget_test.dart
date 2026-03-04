import 'package:flutter/material.dart';
import 'package:flutter_test/flutter_test.dart';

import 'package:colibri_flutter_example/main.dart';

void main() {
  testWidgets('App renders combo-button and log area', (WidgetTester tester) async {
    await tester.pumpWidget(const ColibriExampleApp());

    // The combo-button should show the default test label.
    expect(find.text('Block Number'), findsOneWidget);

    // The play icon should be visible.
    expect(find.byIcon(Icons.play_arrow), findsOneWidget);

    // The dropdown arrow should be present.
    expect(find.byIcon(Icons.arrow_drop_down), findsOneWidget);
  });
}
