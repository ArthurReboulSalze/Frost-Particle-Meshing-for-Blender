"""
Script de test pour vérifier le nombre de threads TBB
À exécuter dans la console Python de Blender après avoir chargé l'addon
"""

try:
    import blender_frost_adapter
    thread_count = blender_frost_adapter.tbb_thread_count
    print(f"✓ TBB configuré pour utiliser {thread_count} threads")
    print(f"✓ Attendu pour un 12c/24t CPU: 24 threads")
    
    if thread_count >= 20:
        print("✓ Configuration optimale détectée!")
    else:
        print(f"⚠ Attention: Seulement {thread_count} threads détectés")
        
except Exception as e:
    print(f"Erreur: {e}")
