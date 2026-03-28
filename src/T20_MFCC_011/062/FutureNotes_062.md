# T20 MFCC 062 Notes

## 이번 단계 성격
062는 061의 runtime config / batch timeout 구조 위에서,
zero-copy / DMA write 준비와 runtime config 양방향 동기화를 올린 단계입니다.

## 이번 단계에서 한 일
- runtime config apply helper 추가
- runtime config JSON ↔ 내부 상태 양방향 반영 강화
- DMA slot staging buffer 구조 추가
- batch push 시 DMA slot staging 반영
- flush 시 DMA slot used 상태 초기화
- Web API에 runtime_config_apply endpoint 추가

## 이번 단계에서 아직 단순화한 것
- 실제 DMA write 호출은 아직 없음
- runtime config 전체 필드를 완전 세밀하게 매핑하진 않음
- LittleFS/SD_MMC별 실제 고성능 비동기 write 최적화는 아직 준비 단계
- selection sync / type-meta / viewer 고도화는 후속 단계

## 다음 단계 권장
1. 062 실제 컴파일 및 실행 확인
2. zero-copy / DMA 실제 write 경로 반영
3. runtime config 전체 필드 동기화 마무리
4. viewer / selection sync / type-meta 고도화
